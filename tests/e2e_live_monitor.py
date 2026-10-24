"""Run against frontend :4173 and a separate --mock Gateway :8765."""
import json
import re
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

OUT = Path('outputs/live-monitor-e2e')
OUT.mkdir(parents=True, exist_ok=True)


def nav(page, name):
    page.get_by_role('navigation').get_by_role('button', name=re.compile(name)).click()


with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(viewport={'width':1440, 'height':1100}, device_scale_factor=1)
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.goto('http://127.0.0.1:4173')
    page.wait_for_load_state('networkidle')
    assert page.request.get('http://127.0.0.1:8765/api/v1/state').json()['source'] == 'demo', 'E2E requires a mock Gateway'
    page.request.post('http://127.0.0.1:8765/api/v1/disconnect')
    page.get_by_role('button', name='Connect', exact=True).click()
    page.get_by_role('button', name='▶ START', exact=True).click()
    expect(page.locator('.metric-value').first).to_contain_text('75')
    page.wait_for_timeout(1500)
    before = int(page.locator('.app-shell').get_attribute('data-revision'))
    page.get_by_role('button', name='Ⅱ Pause display', exact=True).click()
    canvas_before = page.locator('canvas').evaluate('(c)=>c.toDataURL()')
    page.wait_for_timeout(600)
    assert page.locator('canvas').evaluate('(c)=>c.toDataURL()') == canvas_before
    assert int(page.locator('.app-shell').get_attribute('data-revision')) > before
    page.get_by_role('button', name='▶ Resume display', exact=True).click()
    nav(page, 'Settings')
    page.get_by_label('Color theme', exact=True).select_option('dark')
    expect(page.locator('html')).to_have_attribute('data-theme','dark')
    page.get_by_label('Color theme', exact=True).select_option('system')
    page.emulate_media(color_scheme='dark')
    expect(page.locator('html')).to_have_attribute('data-theme','dark')
    nav(page, 'Live Monitor')
    expect(page.get_by_text('● Saving Gateway session', exact=True)).to_be_visible()
    page.screenshot(path=str(OUT/'desktop-dark.png'), full_page=True)
    page.get_by_role('button', name='Toggle color theme').click()
    page.screenshot(path=str(OUT/'desktop-light.png'), full_page=True)
    page.get_by_role('button', name='■ STOP', exact=True).click()
    nav(page, 'Sessions')
    page.get_by_role('button', name='Replay', exact=True).first.click()
    expect(page.locator('.replay')).to_contain_text('packets')
    page.get_by_role('button', name='Pause replay', exact=True).click()
    page.get_by_role('button', name='Restart replay', exact=True).click()
    with page.expect_download() as download:
        page.get_by_role('button', name='JSON', exact=True).first.click()
    path = download.value.path()
    exported = json.loads(Path(path).read_text())
    assert any(row['type']=='waveform' for row in exported['packets'])
    page.get_by_role('button', name='Delete', exact=True).first.click()
    page.get_by_role('button', name='Confirm delete', exact=True).click()
    expect(page.get_by_text('No recordings yet.',exact=False)).to_be_visible()
    nav(page,'Diagnostics')
    page.get_by_role('button',name='Scan devices',exact=True).click()
    expect(page.get_by_text('ECG-Monitor (Demo)',exact=False)).to_be_visible()

    # Real browser transport against byte-level mock backend, not browser Demo.
    nav(page,'Settings')
    page.get_by_label('Connection mode', exact=True).select_option('gateway')
    page.get_by_role('button',name='Apply connection',exact=True).click()
    expect(page.get_by_role('button',name='Apply connection',exact=True)).to_be_enabled()
    nav(page,'Live Monitor')
    page.get_by_role('button',name='Connect',exact=True).click()
    page.get_by_role('button',name='▶ START',exact=True).click()
    expect(page.locator('.metric-value').first).to_contain_text('75')
    state = page.request.get('http://127.0.0.1:8765/api/v1/state').json()
    sid = state['session_id']
    assert sid and state['streaming']
    nav(page,'Settings')
    page.get_by_label('Color theme',exact=True).select_option('dark')
    nav(page,'Diagnostics')
    page.wait_for_timeout(700)
    after = page.request.get('http://127.0.0.1:8765/api/v1/state').json()
    assert after['session_id']==sid and after['counters']['samples']>state['counters']['samples']
    page.reload()
    page.wait_for_load_state('networkidle')
    expect(page.get_by_text('● Saving Gateway session',exact=True)).to_be_visible()
    assert page.request.get('http://127.0.0.1:8765/api/v1/state').json()['session_id']==sid
    page.get_by_role('button',name='■ STOP',exact=True).click()
    nav(page,'Sessions')
    page.get_by_role('button',name='Replay',exact=True).first.click()
    expect(page.locator('.replay')).to_be_visible()
    with page.expect_download() as download:
        page.get_by_role('button',name='CSV',exact=True).first.click()
    assert 'waveform,' in Path(download.value.path()).read_text()
    page.screenshot(path=str(OUT/'sessions.png'),full_page=True)
    nav(page,'Live Monitor')
    page.get_by_role('button',name='Disconnect',exact=True).click()
    page.get_by_role('button',name='Connect',exact=True).click()
    page.get_by_role('button',name='▶ START',exact=True).click()
    expect(page.locator('.metric-value').first).to_contain_text('75')
    page.set_viewport_size({'width':390,'height':844})
    page.screenshot(path=str(OUT/'mobile.png'),full_page=True)
    assert page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')
    page.get_by_role('button',name='■ STOP',exact=True).click()
    page.get_by_role('button',name='Disconnect',exact=True).click()
    assert not errors, errors
    browser.close()
    print('PASS browser E2E: Demo/Gateway, waveform, START/STOP, pause, themes, navigation, reload, sessions/replay/export/delete, reconnect, mobile; no JS errors')
