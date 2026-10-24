"""Reproducible accelerated soak of 30 device minutes (NOT a physical test)."""
import asyncio
import json
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'firmware/esp32/tools'))
from edge_gateway import BATCH, EVENT
from live_gateway import Gateway, SessionStore


async def main():
    with tempfile.TemporaryDirectory() as directory:
        gateway = Gateway(SessionStore(Path(directory)/'soak.db'), mock=True)
        await gateway.start()
        gateway.epoch = 1
        gateway.session = gateway.store.start('demo')
        sid = gateway.session
        fast = asyncio.Queue(maxsize=128)
        slow = asyncio.Queue(maxsize=128)
        gateway.subscribers.update([fast, slow])
        received = 0
        async def drain():
            nonlocal received
            while True:
                message = await fast.get()
                if message['type'] in ('waveform','event'):
                    received += 1
        consumer = asyncio.create_task(drain())
        started = time.perf_counter()
        for i in range(36000):
            t = i*50
            at = datetime.fromtimestamp(1700000000+t/1000, timezone.utc).isoformat()
            await gateway.incoming.put((1, BATCH.pack(1,1,5,10,i & 65535,t,0,10,100,-10,0), at))
            if t % 800 == 200:
                await gateway.incoming.put((1, EVENT.pack(1,2,1,15,t,75,800,92,1,800,100,0), at))
            if i % 50 == 0:
                await asyncio.sleep(0)
        while gateway.counters['packets'] < 38250:
            await asyncio.sleep(.01)
        await asyncio.sleep(.01)
        elapsed = time.perf_counter()-started
        assert gateway.counters['samples']==180000
        assert gateway.counters['missing_packets']==0
        assert gateway.counters['invalid_packets']==0
        assert gateway.counters['ingress_drops']==0
        assert gateway.store.sessions()[0]['packets']==38250
        assert received==38250, received
        assert gateway.counters['websocket_drops']>0  # intentionally stalled consumer
        result={'status':'PASS','mode':'accelerated synthetic; not physical or wall-clock 30-minute test',
                'device_minutes':30,'elapsed_wall_seconds':round(elapsed,3),
                'fast_consumer_packets':received,'persisted_packets':38250,
                'slow_consumer_queue':slow.qsize(),'queue_limit':128,'counters':gateway.counters.copy()}
        gateway.store.finish(sid,'accelerated soak completed')
        gateway.session=None
        consumer.cancel()
        await asyncio.gather(consumer, return_exceptions=True)
        await gateway.close()
        Path('outputs/live-monitor-soak.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result,indent=2))


asyncio.run(main())
