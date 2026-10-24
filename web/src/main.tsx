import {useEffect,useRef,useState} from 'react';
import {createRoot} from 'react-dom/client';
import {DemoTransport,ECGStore,GatewayTransport,emptyState,type Packet,type Session,type State,type Transport} from './model';
import {Waveform} from './Waveform';
import './style.css';

type Settings={theme:'light'|'dark'|'system';seconds:number;gain:number;markers:boolean;mode:'demo'|'gateway';url:string;device:string};
const defaults:Settings={theme:'system',seconds:10,gain:1,markers:true,mode:'demo',url:'http://127.0.0.1:8765',device:'ECG-Monitor'};
function saved():Settings {try{return {...defaults,...JSON.parse(localStorage.getItem('ecg-settings')||'{}')};}catch{return defaults;}}
const number=(value:number|null|undefined)=>value==null?'—':value.toLocaleString();
const stamp=(value:string)=>new Date(value).toLocaleTimeString();

function Replay({session,transport,settings}:{session:Session;transport:Transport;settings:Settings}){
  const [store]=useState(()=>new ECGStore());
  const [playing,setPlaying]=useState(true),[speed,setSpeed]=useState(1),[restart,setRestart]=useState(0);
  const [result,setResult]=useState('Loading session…');
  const [count,setCount]=useState(0);
  const controls=useRef({playing,speed});controls.current={playing,speed};
  useEffect(()=>{
    let cancelled=false,timer:ReturnType<typeof setTimeout>,cursor=0,queue:Packet[]=[],ended=false,elapsed=0,origin:number|undefined,last=performance.now(),total=0;
    store.clear();setCount(0);setResult('Playing');setPlaying(true);
    const run=async()=>{
      if(cancelled)return;
      const now=performance.now();if(controls.current.playing)elapsed+=(now-last)*controls.current.speed;last=now;
      try{
        if(!queue.length&&!ended){queue=await transport.request<Packet[]>(`sessions/${session.id}/packets?after=${cursor}&limit=500`);if(cancelled)return;ended=queue.length===0;if(queue.length){cursor=queue.at(-1)!.cursor!;origin??=Date.parse(queue[0].received_utc);}}
        if(controls.current.playing){let n=0;while(queue.length&&n++<100&&Date.parse(queue[0].received_utc)-origin!<=elapsed){store.ingest(queue.shift()!);total++;}setCount(total);}
        if(ended&&!queue.length){setResult(total?'Replay complete':'Session has no packets');setPlaying(false);return;}
        timer=setTimeout(run,25);
      }catch(error){setResult(String(error));setPlaying(false);}
    };
    void run();return()=>{cancelled=true;clearTimeout(timer);};
  },[transport,session.id,restart,store]);
  const metric=store.latestEvent();
  return <section className="panel replay"><div className="panel-head"><div><span className="eyebrow">SESSION PLAYBACK · {session.source.toUpperCase()}</span><h2>{stamp(session.started_utc)} recording</h2></div><span className="badge">{result} · {count} packets</span></div>
    <Waveform store={store} seconds={settings.seconds} gain={settings.gain} markers={settings.markers}/>
    <div className="toolbar"><button onClick={()=>setPlaying(!playing)}>{playing?'Pause replay':'Play replay'}</button><button onClick={()=>setRestart(restart+1)}>Restart replay</button><label>Speed <select value={speed} onChange={e=>setSpeed(Number(e.target.value))}>{[.5,1,2,4].map(v=><option key={v} value={v}>{v}×</option>)}</select></label><span>BPM {number(metric?.bpm)} · SQI {number(metric?.sqi_score)} · AF {metric?.af||'unknown'}</span></div>
    <p className="footnote">Playback uses saved packet timing. Live connection and recording continue independently.</p>
  </section>;
}

function App(){
  const [settings,setSettings]=useState<Settings>(saved);
  const [draft,setDraft]=useState(()=>saved());
  const [store]=useState(()=>new ECGStore());
  const [transport,setTransport]=useState<Transport>(()=>settings.mode==='gateway'?new GatewayTransport(settings.url):new DemoTransport());
  const [page,setPage]=useState('Live Monitor');
  const [state,setState]=useState<State>(emptyState),[revision,setRevision]=useState(0);
  const [paused,setPaused]=useState(false),[busy,setBusy]=useState(false),[error,setError]=useState('');
  const [sessions,setSessions]=useState<Session[]>([]),[selected,setSelected]=useState<Session|null>(null);
  const [devices,setDevices]=useState<{name:string;address:string}[]>([]);
  const [deleteId,setDeleteId]=useState<string|null>(null);
  useEffect(()=>{
    store.clear();store.state=emptyState();
    transport.open(message=>store.ingest(message),status=>{store.transport=status;});
    const interval=setInterval(()=>{setState({...store.state});setRevision(store.revision);},250);
    return()=>{clearInterval(interval);transport.close();};
  },[transport,store]);
  useEffect(()=>{
    localStorage.setItem('ecg-settings',JSON.stringify(settings));
    const query=matchMedia('(prefers-color-scheme: dark)');
    const apply=()=>document.documentElement.dataset.theme=settings.theme==='system'?(query.matches?'dark':'light'):settings.theme;
    apply();query.addEventListener('change',apply);return()=>query.removeEventListener('change',apply);
  },[settings]);
  useEffect(()=>{
    if(page!=='Sessions')return;
    let active=true;
    const refresh=()=>transport.request<Session[]>('sessions').then(data=>{if(active)setSessions(data);}).catch(e=>{if(active)setError(String(e));});
    void refresh();const timer=setInterval(refresh,2000);return()=>{active=false;clearInterval(timer);};
  },[page,transport]);
  async function perform(action:()=>Promise<unknown>){setBusy(true);setError('');try{await action();}catch(e){setError(e instanceof Error?e.message:String(e));}finally{setBusy(false);}}
  function command(command:string){void perform(()=>transport.request('command','POST',{command}));}
  async function download(session:Session,format:'csv'|'json'){
    const blob=await transport.export(session.id,format);const url=URL.createObjectURL(blob);const a=document.createElement('a');a.href=url;a.download=`ecg-${session.source}-${session.id}.${format}`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  }
  async function applyConnection(){
    if(state.session_id)throw new Error('Stop the active session before changing source');
    const next=draft.mode==='gateway'?new GatewayTransport(draft.url.replace(/\/$/,'')):new DemoTransport();
    if(state.connection==='connected'||state.connection==='reconnecting')await transport.request('disconnect','POST');
    setSelected(null);setSessions([]);setSettings({...settings,mode:draft.mode,url:draft.url.replace(/\/$/,''),device:draft.device});setTransport(next);
  }
  const connected=state.connection==='connected';
  const metric=store.latestEvent();
  const metricAge=metric&&store.samples.length?store.samples.at(-1)!.t-metric.t:Infinity;
  const fresh=store.transport==='online'&&state.streaming&&metricAge<5000;
  const sqi=metric?.sqi_status===1?'Usable':metric?.sqi_status===0?'Unusable':'Insufficient';
  const demo=settings.mode==='demo'||state.source==='demo';
  const title:Record<string,string>={'Live Monitor':'A clear view of every beat.','Sessions':'Your recordings, ready to revisit.','Diagnostics':'Know what is happening.','Settings':'Make this monitor your own.'};
  const timeline=[...store.events].reverse().slice(0,15);
  const metricCard=(label:string,value:string,unit:string,description:string,accent='')=><article className={`metric ${accent}`}><span className="eyebrow">{label}</span><div className="metric-value">{value}<small>{unit}</small></div><span className="metric-note">{description}</span></article>;
  return <div className="app-shell" data-revision={revision}>
    <aside className="sidebar"><a className="brand" href="#" onClick={e=>{e.preventDefault();setPage('Live Monitor');}}><span className="brand-mark">⌁</span><span>pulse<span className="brand-light">view</span><small>ECG LIVE MONITOR</small></span></a>
      <div className="nav-label">WORKSPACE</div><nav aria-label="Main navigation">{[['Live Monitor','◉'],['Sessions','▤'],['Diagnostics','⌘'],['Settings','⚙']].map(([name,icon])=><button className={page===name?'nav-item active':'nav-item'} key={name} onClick={()=>setPage(name)}><span>{icon}</span>{name}{name==='Live Monitor'&&state.streaming&&<i className="dot"/>}</button>)}</nav>
      <div className="sidebar-bottom"><div className="local-card"><span className="local-icon">⌂</span><div><strong>Local-first workspace</strong><p>Your ECG stays on this computer.</p></div></div><p className="version">ECG Live Monitor <span>V1.0</span></p></div>
    </aside>
    <div className="workspace"><header className="topbar"><span>Workspace <span className="breadcrumb">/ {page}</span></span><div><span className={`badge ${demo?'amber':'blue'}`}>{demo?'DEMO · SYNTHETIC DATA':'REAL GATEWAY'}</span><button className="icon-button" aria-label="Toggle color theme" onClick={()=>setSettings({...settings,theme:document.documentElement.dataset.theme==='dark'?'light':'dark'})}>{document.documentElement.dataset.theme==='dark'?'☀':'☾'}</button></div></header>
      <main><div className="page-heading"><div><span className="eyebrow">{page==='Live Monitor'?'REALTIME SIGNAL WORKSPACE':'ECG LIVE MONITOR'}</span><h1>{page}</h1><p>{title[page]}</p></div><div className="status-chip"><i className={`dot ${state.streaming?'':'idle'}`}/>{store.transport!=='online'?'Gateway offline':state.streaming?'Receiving ECG':connected?'Connected · awaiting ECG':state.connection}</div></div>
        {error&&<div className="alert" role="alert"><strong>Action could not complete.</strong> {error}<button aria-label="Dismiss error" onClick={()=>setError('')}>×</button></div>}
        {demo&&<div className="demo-notice"><span>◈</span><div><strong>Demo workspace</strong> Deterministic simulated ECG. {settings.mode==='demo'?'Demo sessions are temporary (8 sessions, 30 minutes each); export before leaving.':'The Gateway is running its mock BLE adapter.'}</div><button onClick={()=>setPage('Settings')}>Change source →</button></div>}
        {page==='Live Monitor'&&<>
          <section className="metrics">{metricCard('HEART RATE',fresh?number(metric?.bpm):'—','bpm',fresh&&metric?.bpm?'Device-reported heart rate':'Awaiting valid, recent telemetry','heart')}{metricCard('RR INTERVAL',fresh?number(metric?.rr_ms):'—','ms','R–R interval from device')}{metricCard('SIGNAL QUALITY',fresh?number(metric?.sqi_score):'—','/ 100',fresh?sqi:'Insufficient or stale telemetry')}{metricCard('AF SCREENING',fresh?(metric?.af==='suspected'?'Suspected':metric?.af==='raw-positive'?'Raw positive':metric?.af==='sqi-invalid'?'Invalid SQI':'Unknown'):'—','','BLE v1 does not transmit AF validity','af')}</section>
          <section className="panel signal-panel"><div className="panel-head"><div><span className="eyebrow">LIVE SIGNAL</span><h2>ECG waveform <span className="badge subtle">Corrected</span></h2></div><div className="signal-meta"><span><i className="dot"/> {demo?'Simulated':'BLE'} · nominal ~100 Hz</span><span>ADC acquisition · 360 Hz</span></div></div>
            <div className="wave-toolbar"><div className="segmented" aria-label="Waveform window">{[5,10,20,30].map(s=><button key={s} className={settings.seconds===s?'selected':''} onClick={()=>setSettings({...settings,seconds:s})}>{s}s</button>)}</div><div><span className="marker-key">▼</span> R-peak <button className={paused?'pause selected':'pause'} onClick={()=>setPaused(!paused)}>{paused?'▶ Resume display':'Ⅱ Pause display'}</button></div></div>
            <Waveform store={store} seconds={settings.seconds} gain={settings.gain} paused={paused} markers={settings.markers}/>
            <div className="wave-footer"><span>{paused?'DISPLAY PAUSED · recording continues':'SCROLLING ECG'} <span className="muted">· ADC counts · {settings.gain}× gain</span></span><span>Gaps are preserved · sample times estimated from BLE batches</span></div>
            <div className="controls"><div className="control-buttons"><button className="primary" disabled={busy||!connected||!!state.session_id||store.transport!=='online'} onClick={()=>command('start')}>▶ START</button><button className="danger-outline" disabled={busy||!connected||store.transport!=='online'} onClick={()=>command('stop')}>■ STOP</button><span className="divider"/><button disabled={busy||connected||store.transport!=='online'} onClick={()=>void perform(()=>transport.request('connect','POST',{name:settings.device}))}>Connect</button><button disabled={busy||state.connection==='disconnected'} onClick={()=>void perform(()=>transport.request('disconnect','POST'))}>Disconnect</button></div><span className={state.session_id?'recording':'muted'}>{state.session_id?'● Saving Gateway session':'No active Gateway session'}</span></div>
          </section>
          <div className="bottom-grid"><section className="panel"><div className="panel-head"><h2>Event timeline</h2><span className="badge subtle">{store.events.length} recent events</span></div><div className="timeline">{timeline.length?timeline.map((e,i)=><div className="timeline-row" key={`${e.t}-${i}`}><span className={`event-dot ${e.af==='suspected'?'red':''}`}/><div><strong>{e.af==='suspected'?'AF screening flag':e.sqi_status===0?'Signal quality warning':e.r_peak?'R-peak detected':'Telemetry update'}</strong><p>{e.sqi_status===1?`RR ${number(e.rr_ms)} ms · SQI ${number(e.sqi_score)}`:'Metrics invalid or insufficient'}</p></div><time>{(e.t/1000).toFixed(2)} s</time></div>):<div className="empty">Events appear as the device reports them.</div>}</div></section>
          <section className="panel device-panel"><div className="panel-head"><h2>Device & acquisition</h2><span className="badge blue">BLE v1</span></div><dl><div><dt>Source</dt><dd>{demo?'Simulated ECG':settings.device}</dd></div><div><dt>BLE connection</dt><dd>{state.connection}</dd></div><div><dt>Actual streaming</dt><dd>{store.transport==='online'&&state.streaming?'Receiving waveform':'No recent waveform'}</dd></div><div><dt>Last command</dt><dd>{state.command_sent?`${state.command_sent.command.toUpperCase()} sent`:'None'}</dd></div><div><dt>Firmware recording</dt><dd>Unacknowledged</dd></div><div><dt>Waveform packets lost</dt><dd>{number(state.counters.missing_packets||0)}</dd></div></dl><div className="info-note">START/STOP request device recording and open/close the Gateway session. BLE v1 cannot confirm the firmware recording state.</div></section></div>
        </>}
        {page==='Sessions'&&<><section className="panel"><div className="panel-head"><div><span className="eyebrow">RECORDING LIBRARY</span><h2>Saved sessions</h2></div><button onClick={()=>void perform(async()=>setSessions(await transport.request('sessions')))}>Refresh</button></div><div className="table-wrap"><table><thead><tr><th>Recording</th><th>Source</th><th>Status</th><th>Packets</th><th>Actions</th></tr></thead><tbody>{sessions.map(s=><tr key={s.id}><td><strong>{new Date(s.started_utc).toLocaleString()}</strong><small>{s.id.slice(0,12)} · {s.reason||'Capture in progress'}</small></td><td><span className={`badge ${s.source==='demo'?'amber':'blue'}`}>{s.source.toUpperCase()}</span></td><td>{s.status}</td><td>{s.packets.toLocaleString()}</td><td><div className="table-actions"><button disabled={s.status==='recording'} onClick={()=>setSelected(s)}>Replay</button><button disabled={s.status==='recording'} onClick={()=>void perform(()=>download(s,'csv'))}>CSV</button><button disabled={s.status==='recording'} onClick={()=>void perform(()=>download(s,'json'))}>JSON</button>{deleteId===s.id?<><button className="danger-outline" onClick={()=>void perform(async()=>{await transport.request(`sessions/${s.id}`,'DELETE');setSessions(await transport.request('sessions'));if(selected?.id===s.id)setSelected(null);setDeleteId(null);})}>Confirm delete</button><button onClick={()=>setDeleteId(null)}>Cancel</button></>:<button className="text-danger" disabled={s.status==='recording'} onClick={()=>setDeleteId(s.id)}>Delete</button>}</div></td></tr>)}</tbody></table>{!sessions.length&&<div className="empty">No recordings yet. Connect and press START on Live Monitor.</div>}</div></section>{selected&&<Replay key={selected.id} session={selected} transport={transport} settings={settings}/>}</>}
        {page==='Diagnostics'&&<><section className="metrics">{metricCard('RECEIVE RATE',state.packets_per_second.toFixed(1),'pkt/s','Rolling 5-second Gateway rate')}{metricCard('VALID PACKETS',number(state.counters.packets||0),'','Since Gateway launch')}{metricCard('PACKET GAPS',number(state.counters.missing_packets||0),'','Missing waveform sequence numbers')}{metricCard('INVALID PACKETS',number(state.counters.invalid_packets||0),'','Rejected by strict BLE decoder')}</section><div className="bottom-grid"><section className="panel"><div className="panel-head"><h2>Transport diagnostics</h2><button disabled={busy} onClick={()=>void perform(async()=>setDevices(await transport.request('devices')))}>Scan devices</button></div><dl><div><dt>Browser transport</dt><dd>{store.transport}</dd></div><div><dt>BLE connection</dt><dd>{state.connection}</dd></div><div><dt>Ingress queue drops</dt><dd>{number(state.counters.ingress_drops||0)}</dd></div><div><dt>Slow-browser drops</dt><dd>{number(state.counters.websocket_drops||0)}</dd></div><div><dt>Reconnections</dt><dd>{number(state.counters.reconnects||0)}</dd></div><div><dt>Buffered samples</dt><dd>{store.samples.length} / 4500</dd></div><div><dt>Last sample age</dt><dd>{state.last_sample_age_s==null?'—':`${state.last_sample_age_s} s`}</dd></div></dl>{devices.map(d=><div className="device-found" key={d.address}>{d.name} <code>{d.address}</code></div>)}<p className="footnote">Events have no sequence field in BLE v1; lost event packets cannot be counted.</p></section><section className="panel"><div className="panel-head"><h2>Gateway log</h2><span className="badge subtle">Latest 100</span></div><div className="logs">{[...state.logs].reverse().map((log,i)=><div key={i}><time>{stamp(log.at)}</time><span>{log.message}</span></div>)}{!state.logs.length&&<div className="empty">No Gateway messages yet.</div>}</div></section></div></>}
        {page==='Settings'&&<div className="settings-grid"><section className="panel"><div className="panel-head"><div><span className="eyebrow">APPEARANCE</span><h2>Display preferences</h2></div></div><div className="settings-body"><label>Color theme<select aria-label="Color theme" value={settings.theme} onChange={e=>setSettings({...settings,theme:e.target.value as Settings['theme']})}><option value="light">Light</option><option value="dark">Dark</option><option value="system">System</option></select></label><label>Waveform window<select value={settings.seconds} onChange={e=>setSettings({...settings,seconds:Number(e.target.value)})}>{[5,10,20,30].map(s=><option key={s} value={s}>{s} seconds</option>)}</select></label><label>Waveform gain<select value={settings.gain} onChange={e=>setSettings({...settings,gain:Number(e.target.value)})}>{[.5,1,2,4].map(s=><option key={s} value={s}>{s}×</option>)}</select></label><label className="check-label"><input type="checkbox" checked={settings.markers} onChange={e=>setSettings({...settings,markers:e.target.checked})}/>Show timestamp-aligned R-peak markers</label><p className="footnote">Display preferences persist on this browser. Theme, navigation and display pause do not interrupt acquisition.</p></div></section><section className="panel"><div className="panel-head"><div><span className="eyebrow">DATA SOURCE</span><h2>Gateway connection</h2></div></div><div className="settings-body"><label>Connection mode<select aria-label="Connection mode" value={draft.mode} onChange={e=>setDraft({...draft,mode:e.target.value as Settings['mode']})}><option value="demo">Deterministic Demo</option><option value="gateway">Real Gateway (BLE)</option></select></label><label>Gateway HTTP URL<input aria-label="Gateway HTTP URL" value={draft.url} onChange={e=>setDraft({...draft,url:e.target.value})} placeholder="http://127.0.0.1:8765"/></label><label>BLE device name<input value={draft.device} onChange={e=>setDraft({...draft,device:e.target.value})}/></label><button className="primary" disabled={busy||!!state.session_id} onClick={()=>void perform(applyConnection)}>Apply connection</button><p className="footnote">Stop recording before changing source. The Python Gateway must remain running. HTTPS automatically uses WSS; remote browser origins require Gateway configuration.</p></div></section></div>}
        <footer><span><span className="footer-pulse">⌁</span> ECG Live Monitor V1</span><span>Research & screening use · Not a medical diagnosis</span></footer>
      </main>
    </div>
  </div>;
}
createRoot(document.getElementById('root')!).render(<App/>);
