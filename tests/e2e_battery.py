"""Browser battery checks against web/dist; isolated synthetic WebSocket, no PCB."""
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from threading import Thread

from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'outputs/battery-e2e'
OUT.mkdir(parents=True, exist_ok=True)


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(QuietHandler, directory=str(ROOT/'web/dist')))
Thread(target=server.serve_forever, daemon=True).start()
try:
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={'width':1440, 'height':1000})
        errors, sockets = [], []
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.add_init_script("localStorage.setItem('ecg-settings',JSON.stringify({mode:'gateway',url:'http://127.0.0.1:8766',theme:'light'}))")
        page.route_web_socket('**/api/v1/ws', lambda ws: sockets.append(ws))
        page.goto(f'http://127.0.0.1:{server.server_port}')
        expect(page.locator('.battery-status')).to_have_attribute('aria-label','Battery unavailable')
        assert sockets
        state = dict(v=1,type='state',source='ble',connection='connected',streaming=False,
                     session_id=None,epoch=1,command_sent=None,device_recording='unknown',
                     counters={},packets_per_second=0,logs=[],battery_age_s=0)
        battery = dict(v=1,type='battery',source='ble',epoch=1,received_utc='test',t=1000,
                       valid=True,millivolts=3750,percent=50,low=False,charging=None,estimated=True)
        def send(**updates):
            sockets[-1].send(json.dumps({**state,'battery':battery,**updates}))
        send()
        expect(page.locator('.battery-status')).to_contain_text('~50%')
        expect(page.locator('.battery-status')).to_have_attribute('title', '3.75 V · Voltage-based estimate; load/charging affect accuracy. Charging state unknown.')
        page.screenshot(path=str(OUT/'light.png'), full_page=True)
        page.get_by_role('button', name='Toggle color theme').click()
        expect(page.locator('html')).to_have_attribute('data-theme','dark')
        battery.update(millivolts=3400,percent=11,low=True)
        send()
        expect(page.locator('.battery-status')).to_contain_text('Low estimate')
        page.set_viewport_size({'width':390,'height':844})
        page.screenshot(path=str(OUT/'dark-mobile-low.png'), full_page=True)
        box = page.locator('.battery-status').bounding_box()
        assert box and box['x'] >= 0 and box['x']+box['width'] <= 390
        send(connection='disconnected')
        expect(page.locator('.battery-status')).to_have_attribute('aria-label','Battery unavailable')
        send(battery={**battery,'valid':False,'percent':None,'millivolts':None})
        expect(page.locator('.battery-status')).not_to_contain_text('Low estimate')
        send(battery_age_s=16)
        expect(page.locator('.battery-status')).to_have_attribute('aria-label','Battery unavailable')
        send(battery={**battery,'percent':101})
        expect(page.locator('.battery-status')).to_have_attribute('aria-label','Battery unavailable')
        send()
        expect(page.locator('.battery-status')).to_contain_text('~11%')
        # No server heartbeat: the local clock must age the last packet too.
        page.wait_for_timeout(15500)
        expect(page.locator('.battery-status')).to_have_attribute('aria-label','Battery unavailable')
        send()
        expect(page.locator('.battery-status')).to_contain_text('~11%')
        sockets[-1].close()
        expect(page.locator('.battery-status')).to_have_attribute('aria-label','Battery unavailable')
        assert not errors, errors
        browser.close()
        print('PASS: battery valid/unknown/invalid/stale/disconnect/low, Light/Dark, mobile; no browser errors')
finally:
    server.shutdown()
    server.server_close()
