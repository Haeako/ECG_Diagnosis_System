export type Sample = {t:number; v:number; gap?:boolean};
export type EventPacket = {v:1; type:'event'; source:string; epoch:number; received_utc:string; t:number; flags:number; r_peak:boolean; bpm:number|null; rr_ms:number|null; bpm_raw:number; rr_raw:number; sqi_score:number|null; sqi_status:number; sqi_rule_mask:number; af_probability_raw:number; af_valid:null; af_score:number; af_event_id:number; af:string; cursor?:number};
export type WavePacket = {v:1; type:'waveform'; source:string; epoch:number; received_utc:string; samples:Sample[]; sequence:number; period_ms:number; missing_packets:number; cursor?:number};
export type Packet = EventPacket | WavePacket;
export type State = {v:1; type:'state'; source:string; connection:string; streaming:boolean; session_id:string|null; epoch:number; command_sent:{command:string;at:string;acknowledged:boolean}|null; device_recording:string; counters:Record<string,number>; packets_per_second:number; logs:{at:string;message:string}[]; last_sample_age_s?:number|null};
export type Message = Packet | State | {v:1;type:'gap';reason:string};
export type Session = {id:string;started_utc:string;ended_utc:string|null;source:string;status:string;reason:string|null;packets:number};

export const emptyState = ():State => ({v:1,type:'state',source:'demo',connection:'disconnected',streaming:false,session_id:null,epoch:0,command_sent:null,device_recording:'unknown',counters:{},packets_per_second:0,logs:[]});

export class ECGStore {
  samples:Sample[]=[];
  events:EventPacket[]=[];
  state=emptyState();
  transport='offline';
  revision=0;
  lastArrival=0;
  private epoch='';
  private gap=false;
  ingest(message:Message) {
    if(message.type==='gap') {this.gap=true; this.revision++; return;}
    if(message.type==='state') {this.state=message; this.revision++; return;}
    const epoch=`${message.source}:${message.epoch}`;
    if(this.epoch!==epoch) {this.samples=[];this.events=[];this.epoch=epoch;this.gap=true;}
    if(message.type==='waveform') {
      const previous=this.samples.at(-1);
      const gap=this.gap || message.missing_packets>0 || !!(previous && message.samples[0].t-previous.t>message.period_ms*2);
      this.samples.push(...message.samples.map((sample,i)=>({...sample,gap:i===0&&gap})));
      if(this.samples.length>4500) this.samples.splice(0,this.samples.length-4500);
      this.gap=false;
      this.lastArrival=performance.now();
    } else {
      this.events.push(message);
      if(this.events.length>200) this.events.splice(0,this.events.length-200);
    }
    this.revision++;
  }
  clear() {this.samples=[];this.events=[];this.epoch='';this.gap=true;this.revision++;}
  // Telemetry is the newest received snapshot, even when its R-peak time is backdated.
  latestEvent() {return this.events.at(-1);}
}

export interface Transport {
  open(handler:(message:Message)=>void, status:(value:string)=>void):void;
  close():void;
  request<T>(path:string, method?:string, body?:unknown):Promise<T>;
  export(id:string, format:'csv'|'json'):Promise<Blob>;
}

export class GatewayTransport implements Transport {
  private socket?:WebSocket;
  private timer?:ReturnType<typeof setTimeout>;
  private stopped=false;
  constructor(readonly base:string) {
    const url=new URL(base);
    if(!['http:','https:'].includes(url.protocol)) throw new Error('Gateway URL must use HTTP or HTTPS');
  }
  open(handler:(message:Message)=>void,status:(value:string)=>void) {
    this.stopped=false;
    const connect=()=>{
      if(this.stopped)return;
      status('connecting');
      const url=new URL(this.base+'/api/v1/ws');url.protocol=url.protocol==='https:'?'wss:':'ws:';
      const socket=this.socket=new WebSocket(url);
      socket.onopen=()=>{status('online');handler({v:1,type:'gap',reason:'WebSocket connected'});};
      socket.onmessage=event=>{
        try {const message=JSON.parse(event.data);if(message.v!==1)throw new Error('Unsupported API version');handler(message);}
        catch {status('invalid API message');}
      };
      socket.onerror=()=>status('gateway unavailable');
      socket.onclose=()=>{status('offline');if(!this.stopped)this.timer=setTimeout(connect,2000);};
    };
    connect();
  }
  close(){this.stopped=true;clearTimeout(this.timer);this.socket?.close();}
  async request<T>(path:string,method='GET',body?:unknown):Promise<T> {
    const response=await fetch(this.base+'/api/v1/'+path,{method,headers:{'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),signal:AbortSignal.timeout(40000)});
    if(!response.ok){const error=await response.json().catch(()=>({detail:response.statusText}));throw new Error(typeof error.detail==='string'?error.detail:JSON.stringify(error.detail));}
    return response.json();
  }
  async export(id:string,format:'csv'|'json') {
    const response=await fetch(`${this.base}/api/v1/sessions/${encodeURIComponent(id)}/export?format=${format}`);
    if(!response.ok)throw new Error(await response.text());
    return response.blob();
  }
}

/** Deterministic UI demo. Same normalized packets; bounded volatile sessions only. */
export class DemoTransport implements Transport {
  private handler:(message:Message)=>void=()=>{};
  private timer?:ReturnType<typeof setInterval>;
  private tick=0;
  private seq=0;
  private packetId=0;
  private sessionStartTick=0;
  private sessions:Session[]=[];
  private packets=new Map<string,Packet[]>();
  state:State={...emptyState(),source:'demo',device_recording:'simulated',counters:{packets:0,samples:0,missing_packets:0,invalid_packets:0,ingress_drops:0,websocket_drops:0}};
  open(handler:(message:Message)=>void,status:(value:string)=>void){this.handler=handler;status('online');this.publish();this.timer=setInterval(()=>this.advance(),50);}
  close(){clearInterval(this.timer);}
  private publish(){this.handler({...this.state,counters:{...this.state.counters},logs:[...this.state.logs]});}
  private emit(message:Packet){
    this.state.counters.packets++;message.cursor=++this.packetId;
    if(this.state.session_id){
      const list=this.packets.get(this.state.session_id)!;
      if(list.length>=40000||this.tick-this.sessionStartTick>=36000){this.finish('demo 30-minute memory limit');this.state.streaming=false;this.publish();return;}
      list.push(message);this.sessions.find(s=>s.id===this.state.session_id)!.packets++;
    }
    this.handler(message);
  }
  advance(){
    if(!this.state.streaming)return;
    const t=this.tick*50;
    const samples=Array.from({length:5},(_,i)=>{const phase=((t+i*10)%800)/800;return {t:t+i*10,v:Math.round(900*Math.exp(-(((phase-.25)/.018)**2))-220*Math.exp(-(((phase-.29)/.025)**2))+150*Math.exp(-(((phase-.55)/.09)**2))+40*Math.sin(phase*Math.PI*2))};});
    const common={v:1 as const,source:'demo',epoch:this.state.epoch,received_utc:new Date(1700000000000+t).toISOString()};
    this.emit({...common,type:'waveform',samples,sequence:this.seq,period_ms:10,missing_packets:0});
    this.state.counters.samples+=5;
    if(t%800===200)this.emit({...common,type:'event',t,flags:1,r_peak:true,bpm:75,rr_ms:800,bpm_raw:75,rr_raw:800,sqi_score:92,sqi_status:1,sqi_rule_mask:15,af_probability_raw:.08,af_valid:null,af_score:100,af_event_id:0,af:'unknown'});
    this.seq=(this.seq+1)&65535;this.tick++;this.state.packets_per_second=21.25;
    if(this.tick%10===0)this.publish();
  }
  private finish(reason:string){if(this.state.session_id){const s=this.sessions.find(s=>s.id===this.state.session_id)!;s.status='complete';s.ended_utc=new Date().toISOString();s.reason=reason;this.state.session_id=null;}}
  async request<T>(path:string,method='GET',body?:unknown):Promise<T>{
    const data=body as {command?:string}|undefined;
    if(path==='state')return this.state as T;
    if(path==='devices')return [{name:'ECG-Monitor (Demo)',address:'SIMULATED'}] as T;
    if(path==='connect'){if(this.state.connection!=='connected')this.state.epoch++;this.state.connection='connected';}
    else if(path==='disconnect'){this.finish('demo disconnected');this.state.connection='disconnected';this.state.streaming=false;}
    else if(path==='command'){
      if(this.state.connection!=='connected')throw new Error('Connect first');
      if(!['start','stop'].includes(data?.command||''))throw new Error('Invalid command');
      const command=data!.command!;
      this.state.command_sent={command,at:new Date().toISOString(),acknowledged:false};
      this.state.streaming=command==='start';
      if(command==='start'&&!this.state.session_id){
        if(this.sessions.length>=8){const old=this.sessions.pop()!;this.packets.delete(old.id);}
        const id=crypto.randomUUID();this.state.session_id=id;this.sessionStartTick=this.tick;this.packets.set(id,[]);
        this.sessions.unshift({id,started_utc:new Date().toISOString(),ended_utc:null,source:'demo',status:'recording',reason:null,packets:0});
      } else if(command==='stop')this.finish('demo stopped');
      this.state.logs.push({at:new Date().toISOString(),message:`Demo ${command}`});this.state.logs=this.state.logs.slice(-100);
    } else if(path==='sessions')return this.sessions.map(s=>({...s})) as T;
    else if(path.startsWith('sessions/')){
      const url=new URL(path,'http://demo/');const [,id,action]=url.pathname.slice(1).split('/');
      if(!this.packets.has(id))throw new Error('Session not found');
      if(method==='DELETE'){if(id===this.state.session_id)throw new Error('Stop recording before deleting');this.packets.delete(id);this.sessions=this.sessions.filter(s=>s.id!==id);return {deleted:id} as T;}
      if(action==='packets'){const after=Number(url.searchParams.get('after')||0);const limit=Number(url.searchParams.get('limit')||500);return this.packets.get(id)!.filter(p=>(p.cursor||0)>after).slice(0,limit) as T;}
    } else throw new Error('Unsupported demo operation');
    this.publish();return this.state as T;
  }
  async export(id:string,format:'csv'|'json'){
    if(id===this.state.session_id)throw new Error('Stop recording before exporting');
    const packets=this.packets.get(id);if(!packets)throw new Error('Session not found');
    if(format==='json')return new Blob([JSON.stringify({session:this.sessions.find(s=>s.id===id),packets},null,2)],{type:'application/json'});
    const rows=['type,timestamp_ms,value,sequence,missing_packets,flags,bpm_raw,rr_raw,sqi_score,sqi_status,af_probability_raw,af_score,af_event_id,af_valid'];
    for(const p of packets){if(p.type==='waveform')for(const s of p.samples)rows.push(['waveform',s.t,s.v,p.sequence,p.missing_packets,...Array(9).fill('')].join(','));else rows.push(['event',p.t,'','','',p.flags,p.bpm_raw,p.rr_raw,p.sqi_score,p.sqi_status,p.af_probability_raw,p.af_score,p.af_event_id,'unknown'].join(','));}
    return new Blob([rows.join('\r\n')],{type:'text/csv'});
  }
}
