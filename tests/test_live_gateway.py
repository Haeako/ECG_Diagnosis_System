"""Hardware-independent protocol and complete mock BLE → WebSocket tests."""
import asyncio
import json
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'firmware/esp32/tools'))
from edge_gateway import BATCH, EVENT, PacketDecoder
from live_gateway import Gateway, MockClient, SessionStore, create_app
from fastapi.testclient import TestClient


def batch(seq=0, t=1000, count=5, period=10):
    return BATCH.pack(1, 1, count, period, seq, t, -32768, -1, 0, 1, 32767)


def event(t=990, flags=1, score=90, status=1):
    return EVENT.pack(1, 2, flags, 15, t, 75, 800, score, status, 8765, 15000, 3)


def test_wire_offsets_and_signed_samples():
    payload = batch(0x1234, 0x12345678)
    assert payload[:10] == bytes.fromhex('0101050a341278563412')
    decoded = PacketDecoder().decode(payload)
    assert [s['v'] for s in decoded['samples']] == [-32768, -1, 0, 1, 32767]
    assert [s['t'] for s in decoded['samples']] == [0x12345678+i*10 for i in range(5)]
    message = PacketDecoder().decode(event())
    assert message['bpm'] == 75 and message['rr_ms'] == 800
    assert message['af_probability_raw'] == .8765 and message['af_score'] == 15000
    assert message['af_valid'] is None and message['af'] == 'unknown'


def test_sequence_rollover_loss_and_rejected_packets_do_not_advance():
    decoder = PacketDecoder()
    decoder.decode(batch(65535))
    assert decoder.decode(batch(0, 1050))['missing_packets'] == 0
    for seq in [0, 65535]:
        with pytest.raises(ValueError):
            decoder.decode(batch(seq, 1100))
    assert decoder.decode(batch(3, 1200))['missing_packets'] == 2


def test_clock_rollover_and_backdated_peak():
    decoder = PacketDecoder()
    decoder.decode(batch(0, 0xFFFFFFF0))
    assert decoder.decode(event(0xFFFFFFE0))['t'] == 0xFFFFFFE0
    assert decoder.decode(batch(1, 34))['samples'][0]['t'] == 2**32+34
    assert decoder.decode(event(4))['t'] == 2**32+4
    with pytest.raises(ValueError, match='clock'):
        decoder.decode(batch(2, 0))


@pytest.mark.parametrize('payload', [b'', bytes(20), batch(count=0), batch(count=6), batch(period=0),
                                    batch(period=11), event(flags=32), event(score=101), event(status=3),
                                    bytes([1, 3])+bytes(18)])
def test_invalid_packets(payload):
    with pytest.raises(ValueError):
        PacketDecoder().decode(payload)


def test_sqi_invalid_and_af_screening():
    decoder = PacketDecoder()
    assert decoder.decode(event(status=0, flags=17))['bpm'] is None
    assert decoder.decode(event(status=2, score=255))['sqi_score'] is None
    assert decoder.decode(event(flags=5))['af'] == 'suspected'
    assert decoder.decode(event(flags=3))['af'] == 'raw-positive'


def test_vertical_slice_and_sessions(tmp_path):
    app = create_app(tmp_path/'gateway.sqlite3', mock=True)
    with TestClient(app) as client:
        assert client.post('/api/v1/command', json={'command':'start'}).status_code == 409
        assert client.get('/api/v1/devices').json()[0]['source'] == 'demo'
        with client.websocket_connect('/api/v1/ws', headers={'origin':'http://localhost:5173'}) as ws:
            assert ws.receive_json()['connection'] == 'disconnected'
            assert client.post('/api/v1/connect', json={}).json()['connection'] == 'connected'
            assert client.post('/api/v1/connect', json={}).status_code == 200
            assert app.state.gateway.client.subscription_count == 1
            result = client.post('/api/v1/command', json={'command':'start'}).json()
            sid = result['session_id']
            assert result['command_sent']['acknowledged'] is False
            messages = []
            while not any(m['type'] == 'event' for m in messages):
                messages.append(ws.receive_json())
            assert any(m['type'] == 'waveform' and m['source']=='demo' for m in messages)
            assert client.delete(f'/api/v1/sessions/{sid}').status_code == 409
            assert client.post('/api/v1/command', json={'command':'erase'}).status_code == 422
            assert client.post('/api/v1/command', json={'command':'stop'}).json()['session_id'] is None
        sessions = client.get('/api/v1/sessions').json()
        assert sessions[0]['status'] == 'complete' and sessions[0]['packets'] > 0
        rows = client.get(f'/api/v1/sessions/{sid}/packets?limit=2').json()
        more = client.get(f'/api/v1/sessions/{sid}/packets?after={rows[-1]["cursor"]}').json()
        assert len(rows) == 2 and more[0]['cursor'] > rows[-1]['cursor']
        assert len(client.get(f'/api/v1/sessions/{sid}/export').json()['packets']) == sessions[0]['packets']
        csv = client.get(f'/api/v1/sessions/{sid}/export?format=csv').text
        assert 'timestamp_ms' in csv and 'event,' in csv
        assert client.post('/api/v1/disconnect').json()['connection'] == 'disconnected'
        assert client.delete(f'/api/v1/sessions/{sid}').json()['deleted'] == sid
        assert client.get(f'/api/v1/sessions/{sid}/packets').status_code == 404


def test_origins(tmp_path):
    with TestClient(create_app(tmp_path/'test.db', mock=True)) as client:
        assert client.post('/api/v1/connect', json={}, headers={'origin':'https://evil.invalid'}).status_code == 403
        from starlette.websockets import WebSocketDisconnect
        with pytest.raises(WebSocketDisconnect):
            with client.websocket_connect('/api/v1/ws', headers={'origin':'https://evil.invalid'}):
                pass


def test_restart_recovery(tmp_path):
    path = tmp_path/'test.db'
    store = SessionStore(path)
    sid = store.start('ble')
    store.append(sid, {'received_utc':'test', 'type':'waveform', 'samples':[]})
    store.db.close()
    reopened = SessionStore(path)
    assert reopened.get(sid)['status'] == 'partial'
    assert len(reopened.packets(sid)) == 1
    reopened.db.close()


def test_queue_overflow_and_disconnect_recovery(tmp_path):
    async def check():
        gateway = Gateway(SessionStore(tmp_path/'test.db'), mock=True)
        await gateway.start()
        await gateway.connect()
        await gateway.command('start')
        sid = gateway.session
        slow = asyncio.Queue(maxsize=3)
        gateway.subscribers.add(slow)
        for _ in range(4):
            gateway.broadcast({'type':'state'})
        assert (await slow.get())['type'] == 'gap'
        assert gateway.counters['websocket_drops'] == 3
        old_client = gateway.client
        await old_client.disconnect()
        await asyncio.sleep(2)
        assert gateway.store.get(sid)['status'] == 'partial'
        assert gateway.connection == 'connected'
        assert gateway.session is None and gateway.command_sent is None
        assert gateway.client.subscription_count == 1
        await gateway.close()
    asyncio.run(check())


def test_command_failure_and_retry_cancellation(tmp_path):
    class FailingCommand(MockClient):
        async def write_gatt_char(self, *args, **kwargs):
            raise RuntimeError('write failed')
    async def check():
        gateway = Gateway(SessionStore(tmp_path/'test.db'), mock=True, client_factory=FailingCommand)
        await gateway.start()
        await gateway.connect()
        with pytest.raises(RuntimeError, match='write failed'):
            await gateway.command('start')
        assert gateway.session is None and gateway.command_sent is None
        assert gateway.store.sessions()[0]['status'] == 'partial'
        await gateway.disconnect()
        await gateway.connect(retry=True)
        assert gateway.client is None and gateway.connection == 'disconnected'
        await gateway.close()
    asyncio.run(check())


def test_notify_failure_cleanup(tmp_path):
    clients = []
    class FailedSubscribe(MockClient):
        async def start_notify(self, *args):
            clients.append(self)
            raise RuntimeError('subscribe failed')
    async def check():
        gateway = Gateway(SessionStore(tmp_path/'test.db'), mock=True, client_factory=FailedSubscribe)
        await gateway.start()
        with pytest.raises(RuntimeError, match='subscribe failed'):
            await gateway.connect()
        assert gateway.connection == 'reconnecting'
        assert not clients[0].is_connected
        await gateway.close()
    asyncio.run(check())


def test_real_adapter_path_uses_existing_finder_and_gatt_uuids(tmp_path):
    from edge_gateway import DATA_UUID, COMMAND_UUID
    calls = []
    class FakeRealClient(MockClient):
        async def start_notify(self, char, callback):
            calls.append(('subscribe', char))
            await super().start_notify(char, callback)
        async def write_gatt_char(self, char, value, response=False):
            calls.append(('write', char, value, response))
            await super().write_gatt_char(char, value, response=response)
    async def finder(name):
        calls.append(('find', name))
        return 'fake-device'
    async def check():
        gateway = Gateway(SessionStore(tmp_path/'test.db'), client_factory=FakeRealClient, finder=finder)
        await gateway.start()
        await gateway.connect('ECG-Test')
        await gateway.command('start')
        await asyncio.sleep(.1)
        assert gateway.counters['samples'] > 0
        assert gateway.store.sessions()[0]['source'] == 'ble'
        await gateway.command('stop')
        await gateway.close()
    asyncio.run(check())
    assert calls[0] == ('find', 'ECG-Test')
    assert ('subscribe', DATA_UUID) in calls
    assert ('write', COMMAND_UUID, b'start', False) in calls
    assert ('write', COMMAND_UUID, b'stop', False) in calls


def test_ingress_overflow_is_counted_and_stale_callbacks_ignored(tmp_path):
    async def check():
        gateway = Gateway(SessionStore(tmp_path/'test.db'), mock=True)
        await gateway.start()
        await gateway.connect()
        callback = gateway.client.callback
        # Fill synchronously before consumer gets control.
        for seq in range(600):
            callback(None, batch(seq, seq*50))
        await asyncio.sleep(.1)
        assert gateway.counters['ingress_drops'] == 88
        assert gateway.counters['packets'] == 512
        await gateway.disconnect()
        count = gateway.counters['packets']
        callback(None, batch(601, 30050))
        await asyncio.sleep(.05)
        assert gateway.counters['packets'] == count
        await gateway.close()
    asyncio.run(check())
