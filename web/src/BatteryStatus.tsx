import type {State} from './model';

export function BatteryStatus({state,online,age}:{state:State;online:boolean;age:number}){
  const battery=state.battery;
  const valid=online&&state.connection==='connected'&&age<15&&battery?.valid
    &&battery.percent!=null&&battery.percent>=0&&battery.percent<=100
    &&battery.millivolts!=null&&battery.millivolts>=2800&&battery.millivolts<=4350;
  const label=valid?`Battery approximately ${battery.percent}%${battery.low?' · Low estimate':''}`:'Battery unavailable';
  const details=valid?`${(battery.millivolts!/1000).toFixed(2)} V · Voltage-based estimate; load/charging affect accuracy. Charging state unknown.`:'No valid recent battery telemetry. Older firmware and Demo may not provide it.';
  return <span className={`battery-status ${valid&&battery.low?'battery-low':''}`} aria-label={label} title={details}><svg viewBox="0 0 28 16" width="28" height="16" aria-hidden="true"><rect x="1" y="1" width="23" height="14" rx="3" fill="none" stroke="currentColor" strokeWidth="1.5"/><path d="M26 5v6" stroke="currentColor" strokeWidth="2"/>{valid&&<rect x="3" y="3" width={19*battery.percent!/100} height="10" rx="1" fill="currentColor"/>}</svg><span>{valid?`~${battery.percent}%`:'—'}</span>{valid&&battery.low&&<small>Low estimate</small>}</span>;
}
