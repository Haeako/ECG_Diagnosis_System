import {describe,it,expect,vi} from 'vitest';
import {DemoTransport,ECGStore,type Message,type Packet,type Session} from './model';

describe('normalized ECG pipeline',()=>{
  it('uses bounded buffers and preserves missing packet gaps and epochs',()=>{
    const store=new ECGStore();
    for(let i=0;i<6000;i++)store.ingest({v:1,type:'waveform',source:'ble',epoch:1,received_utc:'test',samples:[{t:i*10,v:i}],sequence:i,period_ms:10,missing_packets:i===5999?2:0});
    expect(store.samples).toHaveLength(4500);expect(store.samples.at(-1)?.gap).toBe(true);
    store.ingest({v:1,type:'waveform',source:'ble',epoch:2,received_utc:'test',samples:[{t:0,v:0}],sequence:0,period_ms:10,missing_packets:0});
    expect(store.samples).toHaveLength(1);
  });
  it('marks transport loss without fabricating samples',()=>{
    const store=new ECGStore();store.ingest({v:1,type:'gap',reason:'disconnect'});
    expect(store.samples).toHaveLength(0);
    store.ingest({v:1,type:'waveform',source:'ble',epoch:1,received_utc:'test',samples:[{t:123,v:1}],sequence:0,period_ms:10,missing_packets:0});
    expect(store.samples[0]).toEqual({t:123,v:1,gap:true});
  });
  it('Demo uses same pipeline and reproducible timestamps and metrics',async()=>{
    const run=async()=>{
      vi.useFakeTimers();const transport=new DemoTransport(),store=new ECGStore();const packets:Packet[]=[];
      transport.open((m:Message)=>{store.ingest(m);if(m.type==='waveform'||m.type==='event')packets.push(m);},()=>{});
      await transport.request('connect','POST');await transport.request('command','POST',{command:'start'});
      vi.advanceTimersByTime(1000);
      const sid=transport.state.session_id!;
      await transport.request('command','POST',{command:'stop'});
      expect(store.samples.length).toBe(100);expect(store.latestEvent()?.bpm).toBe(75);
      expect(store.latestEvent()?.af_valid).toBe(null);
      const latest=store.latestEvent()!;
      store.ingest({...latest,t:latest.t-100,bpm:null,sqi_status:0});
      expect(store.latestEvent()?.sqi_status).toBe(0);
      const sessions=await transport.request<Session[]>('sessions');expect(sessions[0].status).toBe('complete');
      const first=await transport.request<Packet[]>(`sessions/${sid}/packets?limit=2`);
      const next=await transport.request<Packet[]>(`sessions/${sid}/packets?after=${first.at(-1)!.cursor}&limit=2`);
      expect(next[0].cursor).toBeGreaterThan(first.at(-1)!.cursor!);
      const json=JSON.parse(await (await transport.export(sid,'json')).text());expect(json.packets.length).toBe(packets.length);
      expect(await (await transport.export(sid,'csv')).text()).toContain('event,200');
      await transport.request(`sessions/${sid}`,'DELETE');expect(await transport.request('sessions')).toEqual([]);
      transport.close();vi.useRealTimers();return packets;
    };
    expect(await run()).toEqual(await run());
  });
});
