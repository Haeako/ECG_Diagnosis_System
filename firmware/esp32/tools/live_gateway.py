"""Local-first HTTP/WebSocket extension of edge_gateway (one process/worker)."""
from __future__ import annotations

import asyncio
import contextlib
import csv
import io
import json
import math
import sqlite3
import time
import uuid
from collections import deque
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Literal

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel, Field

from edge_gateway import (BATCH, EVENT, COMMAND_UUID, DATA_UUID, SERVICE_UUID,
                          PacketDecoder, ble_client_options, find_device, open_database)


def utc():
    return datetime.now(timezone.utc).isoformat()


class SessionStore:
    def __init__(self, path):
        self.db = open_database(Path(path))
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS live_sessions(
                id TEXT PRIMARY KEY, started_utc TEXT, ended_utc TEXT,
                source TEXT, status TEXT, reason TEXT);
            CREATE TABLE IF NOT EXISTS live_packets(
                id INTEGER PRIMARY KEY AUTOINCREMENT, session_id TEXT,
                received_utc TEXT, payload TEXT);
            CREATE INDEX IF NOT EXISTS live_packets_session ON live_packets(session_id,id);
        """)
        self.db.execute("UPDATE live_sessions SET status='partial', reason='gateway restart', ended_utc=? WHERE status='recording'", (utc(),))
        self.db.commit()

    def start(self, source):
        sid = uuid.uuid4().hex
        self.db.execute("INSERT INTO live_sessions VALUES(?,?,NULL,?,'recording',NULL)", (sid, utc(), source))
        self.db.commit()
        return sid

    def finish(self, sid, reason, partial=False):
        self.db.execute("UPDATE live_sessions SET ended_utc=?,status=?,reason=? WHERE id=?",
                        (utc(), 'partial' if partial else 'complete', reason, sid))
        self.db.commit()

    def append(self, sid, message):
        self.db.execute("INSERT INTO live_packets(session_id,received_utc,payload) VALUES(?,?,?)",
                        (sid, message['received_utc'], json.dumps(message)))
        # At ~20 packets/s this keeps crash loss bounded to the current transaction.
        self.db.commit()

    def sessions(self):
        return [dict(row) for row in self.db.execute("""SELECT s.*,COUNT(p.id) AS packets
            FROM live_sessions s LEFT JOIN live_packets p ON p.session_id=s.id
            GROUP BY s.id ORDER BY s.started_utc DESC""")]

    def get(self, sid):
        row = self.db.execute("SELECT * FROM live_sessions WHERE id=?", (sid,)).fetchone()
        if row is None:
            raise HTTPException(404, 'Session not found')
        return dict(row)

    def packets(self, sid, after=0, limit=500):
        return [{"cursor": row['id'], **json.loads(row['payload'])} for row in self.db.execute(
            "SELECT id,payload FROM live_packets WHERE session_id=? AND id>? ORDER BY id LIMIT ?",
            (sid, after, limit))]


class MockClient:
    """Deterministic byte-level BLE adapter; no generated data enters real mode."""
    def __init__(self, device, disconnected_callback=None, **kwargs):
        self.is_connected = False
        self.callback = None
        self.disconnected_callback = disconnected_callback
        self.task = None
        self.running = False
        self.seq = 0
        self.tick = 0
        self.subscription_count = 0

    async def connect(self):
        self.is_connected = True

    async def start_notify(self, char, callback):
        if self.callback:
            raise RuntimeError('Duplicate notification subscription')
        self.subscription_count += 1
        self.callback = callback
        self.task = asyncio.create_task(self.generate())

    async def write_gatt_char(self, char, value, response=False):
        if value not in (b'start', b'stop'):
            raise ValueError('Unsupported command')
        self.running = value == b'start'

    async def generate(self):
        while self.is_connected:
            if self.running:
                t = self.tick * 50
                values = []
                for i in range(5):
                    phase = ((t + i * 10) % 800) / 800
                    value = (900 * math.exp(-((phase - .25) / .018)**2)
                             - 220 * math.exp(-((phase - .29) / .025)**2)
                             + 150 * math.exp(-((phase - .55) / .09)**2)
                             + 40 * math.sin(phase * math.tau))
                    values.append(round(value))
                self.callback(None, BATCH.pack(1, 1, 5, 10, self.seq, t & 0xFFFFFFFF, *values))
                if t % 800 == 200:
                    self.callback(None, EVENT.pack(1, 2, 1, 15, t & 0xFFFFFFFF, 75, 800, 92, 1, 800, 100, 0))
                self.seq = (self.seq + 1) & 0xFFFF
                self.tick += 1
            await asyncio.sleep(.05)

    async def disconnect(self):
        self.is_connected = False
        self.running = False
        if self.task:
            self.task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.task
        self.callback = None
        if self.disconnected_callback:
            self.disconnected_callback(self)


class Gateway:
    def __init__(self, store, mock=False, client_factory=None, finder=None):
        self.store = store
        self.mock = mock
        self.client_factory = client_factory
        self.finder = finder
        self.client = None
        self.lock = asyncio.Lock()
        self.decoder = PacketDecoder()
        self.incoming = asyncio.Queue(maxsize=512)
        self.subscribers = set()
        self.logs = deque(maxlen=100)
        self.rates = deque(maxlen=2000)
        self.counters = dict(packets=0, samples=0, missing_packets=0, invalid_packets=0,
                             ingress_drops=0, websocket_drops=0, reconnects=0)
        self.connection = 'disconnected'
        self.command_sent = None
        self.last_sample = None
        self.session = None
        self.desired = False
        self.name = 'ECG-Monitor'
        self.retry_at = 0
        self.retry_delay = 1
        self.tasks = []
        self.epoch = 0

    def log(self, text):
        self.logs.append({"at": utc(), "message": text})

    def state(self):
        now = time.monotonic()
        return {"v": 1, "type": "state", "source": 'demo' if self.mock else 'ble',
                "connection": self.connection,
                "streaming": self.connection == 'connected' and self.last_sample is not None and now - self.last_sample < 2,
                "last_sample_age_s": None if self.last_sample is None else round(now-self.last_sample, 2),
                "command_sent": self.command_sent, "device_recording": "unknown (BLE v1 has no acknowledgement)",
                "session_id": self.session, "epoch": self.epoch,
                "counters": self.counters.copy(), "packets_per_second": sum(t > now-5 for t in self.rates)/5,
                "subscribers": len(self.subscribers), "logs": list(self.logs)}

    def broadcast(self, message):
        for queue in tuple(self.subscribers):
            if queue.full():
                # Drop the slow client's backlog explicitly; browser inserts a discontinuity.
                while not queue.empty():
                    queue.get_nowait()
                    self.counters['websocket_drops'] += 1
                queue.put_nowait({"v": 1, "type": "gap", "reason": "slow browser queue overflow"})
            queue.put_nowait(message)

    async def start(self):
        self.tasks = [asyncio.create_task(self.consume()), asyncio.create_task(self.watch())]

    async def close(self):
        await self.disconnect('gateway shutdown')
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        self.store.db.close()

    async def connect(self, name=None, retry=False):
        async with self.lock:
            if retry and not self.desired:
                return self.state()
            if self.client and self.client.is_connected:
                return self.state()
            self.name = name or self.name
            self.desired = True
            self.connection = 'connecting'
            self.broadcast(self.state())
            loop = asyncio.get_running_loop()
            self.epoch += 1
            epoch = self.epoch
            self.decoder = PacketDecoder()
            self.last_sample = None
            self.command_sent = None
            client = None
            try:
                if self.mock:
                    device = self.name
                    factory = self.client_factory or MockClient
                else:
                    from bleak import BleakClient
                    device = await asyncio.wait_for((self.finder or find_device)(self.name), 12)
                    factory = self.client_factory or BleakClient
                client = factory(device, **ble_client_options())
                self.client = client
                await asyncio.wait_for(client.connect(), 15)
                def notification(sender, payload):
                    def enqueue():
                        if epoch != self.epoch:
                            return
                        try:
                            self.incoming.put_nowait((epoch, bytes(payload), utc()))
                        except asyncio.QueueFull:
                            self.counters['ingress_drops'] += 1
                    loop.call_soon_threadsafe(enqueue)
                await asyncio.wait_for(client.start_notify(DATA_UUID, notification), 10)
                self.connection = 'connected'
                self.retry_delay = 1
                self.log(f'Connected and subscribed: {self.name}')
            except Exception as error:
                self.connection = 'reconnecting'
                self.client = None
                if client:
                    with contextlib.suppress(Exception):
                        await asyncio.wait_for(client.disconnect(), 5)
                self.retry_at = time.monotonic() + self.retry_delay
                self.retry_delay = min(self.retry_delay * 2, 30)
                self.log(f'Connection failed: {type(error).__name__}: {error}')
                self.broadcast(self.state())
                raise RuntimeError(str(error)) from error
            self.broadcast(self.state())
            return self.state()

    async def disconnect(self, reason='user disconnect'):
        async with self.lock:
            self.desired = False
            self.epoch += 1
            client, self.client = self.client, None
            self.connection = 'disconnected'
            if self.session:
                self.store.finish(self.session, reason, partial=True)
                self.session = None
            if client:
                try:
                    await asyncio.wait_for(client.disconnect(), 5)
                except Exception as error:
                    self.log(f'Disconnect error: {error}')
            self.log(reason)
            self.broadcast(self.state())
            return self.state()

    async def command(self, command):
        if command not in ('start', 'stop'):
            raise ValueError('Only start and stop are supported')
        async with self.lock:
            if not self.client or not self.client.is_connected:
                raise RuntimeError('BLE is disconnected')
            new_session = None
            if command == 'start' and not self.session:
                # Open before the write: notifications may arrive while the write awaits.
                new_session = self.session = self.store.start('demo' if self.mock else 'ble')
            try:
                await asyncio.wait_for(self.client.write_gatt_char(COMMAND_UUID, command.encode(), response=False), 5)
            except Exception as error:
                if new_session:
                    self.store.finish(new_session, 'start command failed; device state unknown', partial=True)
                    self.session = None
                self.log(f'Command {command} failed: {error}')
                raise RuntimeError(str(error)) from error
            self.command_sent = {"command": command, "at": utc(), "acknowledged": False}
            if command == 'stop' and self.session:
                self.store.finish(self.session, 'stop command sent; device state unacknowledged')
                self.session = None
            self.log(f'{command.upper()} sent; firmware recording state is unacknowledged')
            self.broadcast(self.state())
            return self.state()

    async def consume(self):
        while True:
            epoch, payload, received = await self.incoming.get()
            if epoch != self.epoch:
                continue
            try:
                message = self.decoder.decode(payload)
            except ValueError as error:
                self.counters['invalid_packets'] += 1
                self.log(str(error))
                continue
            message.update(v=1, source='demo' if self.mock else 'ble', epoch=epoch, received_utc=received)
            self.counters['packets'] += 1
            self.rates.append(time.monotonic())
            if message['type'] == 'waveform':
                self.last_sample = time.monotonic()
                self.counters['samples'] += len(message['samples'])
                self.counters['missing_packets'] += message['missing_packets']
            if self.session:
                try:
                    self.store.append(self.session, message)
                except sqlite3.Error as error:
                    self.log(f'Recording failed: {error}')
                    with contextlib.suppress(sqlite3.Error):
                        self.store.finish(self.session, 'storage error', partial=True)
                    self.session = None
            self.broadcast(message)

    async def watch(self):
        while True:
            await asyncio.sleep(.5)
            if self.connection == 'connected' and self.client and not self.client.is_connected:
                async with self.lock:
                    self.epoch += 1
                    old_client = self.client
                    self.client = None
                    if old_client:
                        with contextlib.suppress(Exception):
                            await asyncio.wait_for(old_client.disconnect(), 5)
                    self.connection = 'reconnecting' if self.desired else 'disconnected'
                    if self.session:
                        self.store.finish(self.session, 'BLE link lost', partial=True)
                        self.session = None
                    self.log('BLE link lost; session closed as partial')
                    self.retry_at = time.monotonic() + 1
            if self.desired and self.connection == 'reconnecting' and time.monotonic() >= self.retry_at:
                self.counters['reconnects'] += 1
                with contextlib.suppress(RuntimeError):
                    await self.connect(retry=True)
            self.broadcast(self.state())


class ConnectBody(BaseModel):
    name: str = Field(default='ECG-Monitor', min_length=1, max_length=100)


class CommandBody(BaseModel):
    command: Literal['start', 'stop']


def create_app(database=Path('ecg_gateway.sqlite3'), mock=False, origins=None, **gateway_options):
    allowed = origins or ['http://localhost:5173', 'http://127.0.0.1:5173',
                          'http://localhost:4173', 'http://127.0.0.1:4173']

    @asynccontextmanager
    async def lifespan(app):
        app.state.gateway = Gateway(SessionStore(database), mock, **gateway_options)
        await app.state.gateway.start()
        yield
        await app.state.gateway.close()

    app = FastAPI(title='ECG Live Monitor', version='1.0.0', lifespan=lifespan)
    app.add_middleware(CORSMiddleware, allow_origins=allowed, allow_methods=['GET', 'POST', 'DELETE'], allow_headers=['Content-Type'])

    @app.middleware('http')
    async def origin_guard(request: Request, call_next):
        origin = request.headers.get('origin')
        if origin and origin not in allowed:
            return JSONResponse({'detail': 'Browser origin not allowed'}, status_code=403)
        if request.headers.get('sec-fetch-site') == 'cross-site' and not origin:
            return JSONResponse({'detail': 'Cross-site request rejected'}, status_code=403)
        return await call_next(request)

    def gateway():
        return app.state.gateway

    @app.get('/api/v1/state')
    async def state():
        return gateway().state()

    @app.get('/api/v1/devices')
    async def devices():
        if mock:
            return [{"name": "ECG-Monitor", "address": "DEMO", "source": "demo"}]
        from bleak import BleakScanner
        try:
            found = await BleakScanner.discover(timeout=5, return_adv=True)
            return [{"name": adv.local_name or device.name, "address": device.address}
                    for device, adv in found.values()
                    if SERVICE_UUID in [s.lower() for s in adv.service_uuids] or (adv.local_name or device.name) == 'ECG-Monitor']
        except Exception as error:
            gateway().log(f'Scan failed: {error}')
            raise HTTPException(503, str(error)) from error

    @app.post('/api/v1/connect')
    async def connect(body: ConnectBody):
        try:
            return await gateway().connect(body.name)
        except RuntimeError as error:
            raise HTTPException(503, str(error)) from error

    @app.post('/api/v1/disconnect')
    async def disconnect():
        return await gateway().disconnect()

    @app.post('/api/v1/command')
    async def command(body: CommandBody):
        try:
            return await gateway().command(body.command)
        except RuntimeError as error:
            raise HTTPException(409, str(error)) from error

    @app.get('/api/v1/sessions')
    async def sessions():
        return gateway().store.sessions()

    @app.get('/api/v1/sessions/{sid}/packets')
    async def packets(sid: str, after: int = 0, limit: int = 500):
        gateway().store.get(sid)
        if after < 0 or not 1 <= limit <= 2000:
            raise HTTPException(422, 'Invalid pagination')
        return gateway().store.packets(sid, after, limit)

    @app.delete('/api/v1/sessions/{sid}')
    async def delete(sid: str):
        store = gateway().store
        store.get(sid)
        if sid == gateway().session:
            raise HTTPException(409, 'Stop recording before deleting')
        with store.db:
            store.db.execute('DELETE FROM live_packets WHERE session_id=?', (sid,))
            store.db.execute('DELETE FROM live_sessions WHERE id=?', (sid,))
        return {"deleted": sid}

    @app.get('/api/v1/sessions/{sid}/export')
    async def export(sid: str, format: Literal['json', 'csv'] = 'json'):
        store = gateway().store
        metadata = store.get(sid)
        if sid == gateway().session:
            raise HTTPException(409, 'Stop recording before exporting')
        async def chunks():
            first = True
            if format == 'json':
                yield '{"session":' + json.dumps(metadata) + ',"packets":['
            else:
                yield 'type,received_utc,timestamp_ms,value,sequence,missing_packets,flags,bpm_raw,rr_raw,sqi_score,sqi_status,af_probability_raw,af_score,af_event_id,af_valid\r\n'
            cursor = 0
            while True:
                rows = store.packets(sid, cursor)
                if not rows:
                    break
                for row in rows:
                    cursor = row['cursor']
                    if format == 'json':
                        yield ('' if first else ',') + json.dumps(row)
                        first = False
                    else:
                        buffer = io.StringIO()
                        writer = csv.writer(buffer)
                        if row['type'] == 'waveform':
                            for item in row['samples']:
                                writer.writerow(['waveform', row['received_utc'], item['t'], item['v'], row['sequence'], row['missing_packets']] + ['']*9)
                        else:
                            writer.writerow(['event', row['received_utc'], row['t'], '', '', '', row['flags'], row['bpm_raw'], row['rr_raw'], row['sqi_score'], row['sqi_status'], row['af_probability_raw'], row['af_score'], row['af_event_id'], 'unknown'])
                        yield buffer.getvalue()
                await asyncio.sleep(0)
            if format == 'json':
                yield ']}'
        return StreamingResponse(chunks(), media_type='application/json' if format == 'json' else 'text/csv',
                                 headers={'Content-Disposition': f'attachment; filename="ecg-{sid}.{format}"'})

    @app.websocket('/api/v1/ws')
    async def websocket(socket: WebSocket):
        if socket.headers.get('origin') not in allowed:
            await socket.close(code=1008)
            return
        await socket.accept()
        queue = asyncio.Queue(maxsize=128)
        controller = gateway()
        controller.subscribers.add(queue)
        async def receive():
            while True:
                await socket.receive_text()
        reader = asyncio.create_task(receive())
        try:
            await socket.send_json(controller.state())
            while not reader.done():
                try:
                    message = await asyncio.wait_for(queue.get(), 1)
                except asyncio.TimeoutError:
                    continue
                await asyncio.wait_for(socket.send_json(message), 5)
        except (WebSocketDisconnect, RuntimeError, asyncio.TimeoutError):
            pass
        finally:
            controller.subscribers.discard(queue)
            reader.cancel()
            await asyncio.gather(reader, return_exceptions=True)

    return app
