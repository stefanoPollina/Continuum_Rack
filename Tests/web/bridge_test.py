"""Runs the real Continuum Rack page with the plugin bridge in headless Chrome, against a mock
of JUCE's window.__JUCE__.backend, and checks every message in both directions.

usage: python3 Tests/web/bridge_test.py [--chrome /usr/bin/google-chrome] [--capture out.json]
needs: pip install playwright (and a Chrome/Chromium); uses Resources/web/index.html.gz"""
import argparse, gzip, json, math, os, sys, tempfile
from playwright.sync_api import sync_playwright
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap = argparse.ArgumentParser()
ap.add_argument('--chrome', default=None)
ap.add_argument('--capture', default=None, help='write the first rackState payload here')
ap.add_argument('--shot', default=None, help='screenshot after the status test')
args = ap.parse_args()
html = gzip.open(os.path.join(root, 'Resources/web/index.html.gz'), 'rt', encoding='utf-8').read()
bridge = open(os.path.join(root, 'Resources/web/plugin-bridge.js'), encoding='utf-8').read()
i = html.rfind('</body>')
patched = html[:i] + '<script>\n' + bridge + '\n</script>\n' + html[i:]   # as WebRackView::buildPage does
page_path = os.path.join(tempfile.gettempdir(), 'continuum-rack-patched.html')
open(page_path, 'w', encoding='utf-8').write(patched)
MOCK = """window.__JUCE__={backend:{sent:[],listeners:{},emitEvent(id,p){this.sent.push([id,JSON.parse(JSON.stringify(p))])},
addEventListener(id,fn){(this.listeners[id]=this.listeners[id]||[]).push(fn);return [id,0]},fire(id,p){(this.listeners[id]||[]).forEach(f=>f(p))}}};"""
errors=[]; fails=0
def check(ok, what):
    global fails
    print(('PASS ' if ok else 'FAIL ')+what)
    if not ok: fails+=1
with sync_playwright() as p:
    b = p.chromium.launch(executable_path=args.chrome, args=['--no-sandbox']) if args.chrome else p.chromium.launch()
    pg = b.new_page(viewport={'width':1600,'height':1000})
    pg.on('pageerror', lambda e: errors.append(str(e)))
    pg.on('console', lambda m: errors.append(m.text) if m.type=='error' else None)
    pg.add_init_script(MOCK)
    pg.goto('file://' + page_path); pg.wait_for_timeout(600)
    sent = lambda: pg.evaluate("window.__JUCE__.backend.sent.map(x=>x[0])")
    ev = lambda name: pg.evaluate(f"window.__JUCE__.backend.sent.filter(x=>x[0]==='{name}').map(x=>x[1])")
    clear = lambda: pg.evaluate("window.__JUCE__.backend.sent=[]")
    s = sent()
    check('rackHello' in s, 'page says hello on start')
    st = ev('rackState')
    check(len(st)==1 and json.loads(st[0]['stateJson'])['tempo']==118, 'initial rack state pushed once (Squelch Dub, 118 BPM)')
    if args.capture:
        json.dump(st[0], open(args.capture, 'w'))
    check(pg.evaluate("ctx && ctx.state")=='running', 'page believes audio is running')
    check(pg.evaluate("document.getElementById('midiDev').textContent")=='HOST MIDI', 'MIDI IN module shows HOST MIDI')
    # no traffic when idle
    clear(); pg.wait_for_timeout(400)
    check('rackState' not in sent(), 'no state traffic while idle')
    # scene change in the page
    clear(); pg.evaluate("loadScene(SCENES.findIndex(s=>s.name==='Psytrance'))"); pg.wait_for_timeout(300)
    st = ev('rackState')
    check(len(st)>=1 and json.loads(st[-1]['stateJson'])['tempo']==145 and st[-1]['sceneIndex']==pg.evaluate("SCENES.findIndex(s=>s.name==='Psytrance')"), 'scene load in the page reaches the plugin')
    # knob edit through the page's own setParam (same path as dragging)
    clear(); pg.evaluate("setParam('b:2:level', 33)"); pg.wait_for_timeout(250)
    st = ev('rackState')
    check(len(st)==1 and json.loads(st[0]['stateJson'])['banks'][2]['level']==33, 'bank knob edit pushed')
    # edits that only touch state (DSP editor) also arrive
    clear(); pg.evaluate("setParam('e:0:0:cutoff', 12)"); pg.wait_for_timeout(250)
    st = ev('rackState')
    check(len(st)==1 and json.loads(st[0]['stateJson'])['banks'][0]['slots'][0]['edit']['cutoff']==12, 'engine edit pushed')
    # RUN SEQ button
    clear(); pg.locator('[data-act="run"]:visible').first.click(); pg.wait_for_timeout(150)
    check(ev('rackRun')==[{'run':True}], 'RUN SEQ sends run on')
    pg.locator('[data-act="run"]:visible').first.click(); pg.wait_for_timeout(150)
    check(ev('rackRun')[-1]=={'run':False}, 'second press sends run off')
    # computer keyboard note
    clear(); pg.mouse.click(5, 990); pg.keyboard.press('a'); pg.wait_for_timeout(150)
    n = ev('rackNote')
    check(len(n)==1 and n[0]['bank']==-1 and n[0]['midi']==48, 'computer key A plays C3 on the whole rack')
    clear(); pg.evaluate("ACT.playBank('3')"); pg.wait_for_timeout(150)
    n = ev('rackNote')
    check(len(n)==1 and n[0]['bank']==3 and n[0]['slot']==-1 and abs(n[0]['duration']-.42)<1e-9, 'PLAY BANK 4 sends a bank note')
    clear(); pg.evaluate("ACT.audition('1,5')"); pg.wait_for_timeout(150)
    n = ev('rackNote'); check(len(n)==1 and n[0]['bank']==1 and n[0]['slot']==5, 'engine audition sends one slot')
    # save goes to the plugin
    clear(); pg.evaluate("ACT.export()"); pg.wait_for_timeout(150)
    sv = ev('rackSave'); check(len(sv)==1 and json.loads(sv[0]['json'])['format']=='continuum-synth-p01', 'CORE SAVE hands the patch to the plugin')
    # plugin -> page: status
    clear()
    status = {'sr':44100,'run':True,'tick':5,'hits':[0]*64,'banks':[0.2]*8,'master':[math.sin(i/8) for i in range(256)],
              'octoBank':0,'octo':[[math.sin(i/(4+s))*0.3 for i in range(128)] for s in range(8)],'midiNote':-1}
    pg.evaluate("d=>window.__JUCE__.backend.fire('rackStatus',d)", status)
    status['tick']=6; status['hits']=[1]+[0]*63; status['midiNote']=60
    pg.evaluate("d=>window.__JUCE__.backend.fire('rackStatus',d)", status); pg.wait_for_timeout(200)
    check(pg.evaluate("!!seqTimer") and pg.evaluate("document.querySelectorAll('.play').length")>0, 'plugin status lights the RUN LED and playhead')
    check(pg.evaluate("ui.hits[0][0]>0"), 'engine hit counter flashes the engine LED')
    check(pg.evaluate("document.getElementById('midiNote').textContent")=='C4', 'host MIDI note shown in MIDI IN')
    check(any(x[0]=='rackScope' for x in pg.evaluate("window.__JUCE__.backend.sent")), 'octoscope bank requested')
    pg.wait_for_timeout(300)
    if args.shot: pg.screenshot(path=args.shot)
    # plugin -> page: params (host automation) without echo
    clear(); pg.evaluate("window.__JUCE__.backend.fire('rackParams',{master:77,lfoWave:'square',banks:[{level:11,mute:true}]})"); pg.wait_for_timeout(300)
    check(pg.evaluate("state.master")==77 and pg.evaluate("state.lfoWave")=='square' and pg.evaluate("state.banks[0].level")==11 and pg.evaluate("state.bankPatterns[0].mute"), 'host automation updates the page')
    check('rackState' not in sent(), 'automation is not echoed back')
    # plugin -> page: full load
    st = json.loads(pg.evaluate("JSON.stringify(state)")); st['tempo']=99; st['banks'][7]['slots'][7]['on']=True
    clear(); pg.evaluate("d=>window.__JUCE__.backend.fire('rackLoad',d)", {'stateJson':json.dumps(st),'sceneIndex':2,'name':'Test Load'}); pg.wait_for_timeout(300)
    check(pg.evaluate("state.tempo")==99 and pg.evaluate("state.banks[7].slots[7].on") and pg.evaluate("ui.scene")==2, 'plugin load rebuilds the rack')
    check('rackState' not in sent(), 'plugin load is not echoed back')
    status['run']=False
    pg.evaluate("d=>window.__JUCE__.backend.fire('rackStatus',d)", status); pg.wait_for_timeout(100)
    check(not pg.evaluate("!!seqTimer") and pg.evaluate("document.querySelectorAll('.play').length")==0, 'stop clears RUN LED and playhead')
    # panic / kill
    clear(); pg.evaluate("ACT.stop(); ACT.kill()"); pg.wait_for_timeout(100)
    check(len(ev('rackPanic'))==2 and pg.evaluate("ctx && ctx.state")=='running', 'PANIC and KILL stop voices but keep the page live')
    check(not errors, 'no page errors: ' + '; '.join(errors[:5]))
    b.close()
print('fails', fails); sys.exit(1 if fails else 0)
