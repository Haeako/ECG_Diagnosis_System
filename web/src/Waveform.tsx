import {useEffect,useRef} from 'react';
import {ECGStore,type Sample,type EventPacket} from './model';

export type ScaleMode='auto'|'fixed';
type ViewOptions={seconds:number;gain:number;scaleMode:ScaleMode;autoCenter:boolean;fixedRange:number;markers:boolean};
type Scale={baseline:number;half:number;targetBaseline:number;targetHalf:number;shrinkCount:number};
const clamp=(v:number,lo:number,hi:number)=>Math.max(lo,Math.min(hi,v));

// Display statistics only: sort a copy, at most twice/second, never change samples.
export function estimateScale(samples:readonly Sample[],autoCenter:boolean) {
  const values=samples.filter(s=>Number.isFinite(s.v)&&Number.isFinite(s.t)).map(s=>s.v).sort((a,b)=>a-b);
  if(!values.length)return {baseline:0,half:100};
  const n=values.length,baseline=(values[Math.floor((n-1)/2)]+values[Math.floor(n/2)])/2;
  // Discard isolated extremes while retaining narrow, recurring QRS peaks.
  const trim=n>=20?Math.max(1,Math.floor(n*.005)):0;
  const center=autoCenter?baseline:0;
  const extent=Math.max(Math.abs(values[trim]-center),Math.abs(values[n-1-trim]-center));
  return {baseline,half:clamp(extent*1.3,50,65536)};
}

export function updateScale(previous:Scale|undefined,estimate:ReturnType<typeof estimateScale>):Scale {
  if(!previous)return {...estimate,targetBaseline:estimate.baseline,targetHalf:estimate.half,shrinkCount:0};
  const next={...previous,targetBaseline:estimate.baseline};
  if(estimate.half>previous.targetHalf*1.1){
    next.half=Math.max(previous.half,estimate.half);next.targetHalf=estimate.half;next.shrinkCount=0;
  }else if(estimate.half<previous.targetHalf*.7){
    if(++next.shrinkCount>=3){next.targetHalf=estimate.half;next.shrinkCount=0;}
  }else next.shrinkCount=0;
  return next;
}

// Gain always multiplies displacement from center; ticks use the inverse bounds.
export function coordinateTransform(center:number,half:number,gain:number,top:number,height:number) {
  const radius=half/gain;
  return {min:center-radius,max:center+radius,
    y:(value:number)=>top+height/2-(value-center)*gain*height/(2*half),
    value:(y:number)=>center+(top+height/2-y)*2*half/(gain*height)};
}

export function niceTicks(min:number,max:number):number[] {
  const power=10**Math.floor(Math.log10((max-min)/5));
  let best:number[]=[],score=Infinity;
  for(const multiplier of [1,1.25,1.5,2,2.5,3,4,5,6,8,10]){
    const step=multiplier*power,start=Math.ceil(min/step-1e-9),end=Math.floor(max/step+1e-9);
    const count=end-start+1;if(count<5||count>7)continue;
    const cost=Math.abs(count-6)+([1,2,5,10].includes(multiplier)?0:.1);
    if(cost<score){score=cost;best=Array.from({length:count},(_,i)=>Number(((start+i)*step).toPrecision(12)));}
  }
  return best;
}

// A marker needs a real nearby sample and an unbroken bracket; never invent a peak.
export function markerSample(samples:readonly Sample[],timestamp:number):Sample|undefined {
  let lo=0,hi=samples.length;
  while(lo<hi){const mid=(lo+hi)>>>1;if(samples[mid].t<timestamp)lo=mid+1;else hi=mid;}
  const right=samples[lo],left=samples[lo-1];
  if(right?.t===timestamp)return !right.gap&&Number.isFinite(right.v)?right:undefined;
  if(!left||!right||right.gap||right.t-left.t>30||right.t<=left.t)return;
  const nearest=timestamp-left.t<=right.t-timestamp?left:right;
  return Math.abs(nearest.t-timestamp)<=15&&Number.isFinite(nearest.v)?nearest:undefined;
}

export function Waveform({store,seconds,gain,paused=false,markers=true,scaleMode='auto',autoCenter=true,fixedRange=1000}: {
  store:ECGStore;seconds:number;gain:number;paused?:boolean;markers?:boolean;scaleMode?:ScaleMode;autoCenter?:boolean;fixedRange?:number;
}) {
  const canvas=useRef<HTMLCanvasElement>(null);
  const options=useRef({seconds,gain,paused,markers,scaleMode,autoCenter,fixedRange});
  options.current={seconds,gain,paused,markers,scaleMode,autoCenter,fixedRange};
  useEffect(()=>{
    let frame=0,lastDraw=0,lastStats=-Infinity,scale:Scale|undefined,statsSample:Sample|undefined;
    let lastArray:Sample[]|undefined,lastSample:Sample|undefined,cutoff:Sample|undefined;
    let epoch='',session:string|null=null,statsKey='';
    let display:{samples:Sample[];events:EventPacket[];view:ViewOptions;center:number;half:number;baseline:number}|undefined;
    const render=(now:number)=>{
      frame=requestAnimationFrame(render);if(now-lastDraw<32)return;
      const dt=Math.min(now-lastDraw,250);lastDraw=now;
      const element=canvas.current;if(!element)return;
      const width=element.clientWidth,height=element.clientHeight;if(width<100||height<80)return;
      const ratio=window.devicePixelRatio||1;
      if(element.width!==Math.round(width*ratio)||element.height!==Math.round(height*ratio)){element.width=Math.round(width*ratio);element.height=Math.round(height*ratio);}
      const ctx=element.getContext('2d');if(!ctx)return;
      const current=options.current;
      if(!current.paused||!display){
        const nextEpoch=`${store.state.source}:${store.state.epoch}`;
        const newArray=lastArray!==store.samples;
        const newestTime=store.samples.at(-1)?.t;
        const timeReset=lastSample&&newestTime!==undefined&&newestTime<lastSample.t;
        if(newArray||timeReset||nextEpoch!==epoch||(store.state.session_id!==null&&store.state.session_id!==session)){
          cutoff=newArray?undefined:timeReset?lastSample:store.samples.at(-1);
          scale=undefined;lastStats=-Infinity;statsSample=undefined;
        }
        lastArray=store.samples;epoch=nextEpoch;session=store.state.session_id;
        const end=store.samples.at(-1)?.t??0,begin=end-current.seconds*1000;
        const offset=cutoff?store.samples.indexOf(cutoff)+1:0;
        const samples=store.samples.slice(offset).filter(s=>Number.isFinite(s.t)&&s.t>=begin);
        lastSample=store.samples.at(-1);
        const key=`${current.seconds}:${current.scaleMode}:${current.autoCenter}`;
        const newest=samples.at(-1);
        if(key!==statsKey||(!scale&&samples.length>0)||(newest!==statsSample&&now-lastStats>=500)){
          scale=updateScale(key!==statsKey?undefined:scale,estimateScale(samples,current.scaleMode==='auto'&&current.autoCenter));
          lastStats=now;statsSample=newest;statsKey=key;
        }
        if(!samples.length)scale=undefined;
        if(scale){
          scale.baseline+=(scale.targetBaseline-scale.baseline)*(1-Math.exp(-dt/800));
          scale.half+=(scale.targetHalf-scale.half)*(1-Math.exp(-dt/1500));
        }
        const centered=current.scaleMode==='auto'&&current.autoCenter;
        display={samples,events:[...store.events],view:{...current},baseline:scale?.baseline??0,
          center:centered?(scale?.baseline??0):0,
          half:current.scaleMode==='fixed'?current.fixedRange:(scale?.half??100)+(centered&&scale?Math.abs(scale.targetBaseline-scale.baseline):0)};
      }
      const {samples,events,view,center,half,baseline}=display;
      const left=64,right=width-14,top=30,bottom=height-30,plotWidth=right-left,plotHeight=bottom-top;
      const transform=coordinateTransform(center,half,view.gain,top,plotHeight);
      const end=samples.at(-1)?.t??0,begin=end-view.seconds*1000;
      const x=(t:number)=>left+(t-begin)/(view.seconds*1000)*plotWidth;
      const style=getComputedStyle(element),grid=style.getPropertyValue('--grid'),ink=style.getPropertyValue('--muted');
      ctx.setTransform(ratio,0,0,ratio,0,0);ctx.clearRect(0,0,width,height);
      ctx.font='10px ui-monospace, monospace';ctx.fillStyle=ink;
      ctx.textAlign='left';ctx.fillText('ADC counts (corrected)',8,16);
      const ticks=niceTicks(transform.min,transform.max);
      ctx.strokeStyle=grid;ctx.lineWidth=.8;ctx.beginPath();
      ctx.textAlign='right';
      for(const tick of ticks){const y=transform.y(tick);ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.fillText(String(tick),left-8,y+3);}
      const major=view.seconds<=5?1:view.seconds<=10?2:5;
      const minor=major/5;
      for(let t=Math.ceil(begin/(minor*1000))*minor*1000;t<=end;t+=minor*1000){ctx.moveTo(x(t),top);ctx.lineTo(x(t),bottom);}ctx.stroke();
      ctx.textAlign='center';
      for(let t=Math.ceil(begin/(major*1000))*major*1000;t<=end;t+=major*1000){
        const px=x(t);ctx.fillText(`${t/1000}s`,clamp(px,left+12,right-12),height-10);
      }
      ctx.save();ctx.beginPath();ctx.rect(left,top,plotWidth,plotHeight);ctx.clip();
      const reference=(value:number,color:string,dashes:number[])=>{
        ctx.strokeStyle=color;ctx.setLineDash(dashes);ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(left,transform.y(value));ctx.lineTo(right,transform.y(value));ctx.stroke();ctx.setLineDash([]);
      };
      reference(0,ink,[]);if(samples.length)reference(baseline,style.getPropertyValue('--baseline'),[5,4]);
      ctx.strokeStyle=style.getPropertyValue('--trace');ctx.lineWidth=1.7;ctx.lineJoin='round';ctx.beginPath();
      let previous:Sample|undefined,clipped=false;
      for(const sample of samples){
        if(!Number.isFinite(sample.v)){previous=undefined;continue;}
        const px=x(sample.t),py=transform.y(sample.v);
        if(!previous||sample.gap||sample.t<=previous.t||sample.t-previous.t>30)ctx.moveTo(px,py);else ctx.lineTo(px,py);
        previous=sample;clipped ||= sample.v<transform.min||sample.v>transform.max;
      }
      ctx.stroke();
      if(view.markers){ctx.fillStyle='#ed4d65';for(const event of events){
        if(!event.r_peak||event.t<begin||event.t>end)continue;
        const sample=markerSample(samples,event.t);if(!sample)continue;
        const px=x(event.t),py=transform.y(sample.v);if(py<top||py>bottom)continue;
        const direction=py<top+15?1:-1;
        ctx.beginPath();ctx.moveTo(px,py+direction*3);ctx.lineTo(px-4,py+direction*10);ctx.lineTo(px+4,py+direction*10);ctx.closePath();ctx.fill();
      }}
      ctx.restore();
      ctx.fillStyle=ink;
      if(!samples.length){ctx.textAlign='center';ctx.fillText('Waiting for ECG samples',left+plotWidth/2,top+plotHeight/2);}
      if(clipped){ctx.textAlign='right';ctx.fillText('Clipped',right,16);}
      element.setAttribute('aria-label',`ECG waveform; ADC counts (corrected); ${view.scaleMode} scale; center ${center.toFixed(1)}; range ${transform.min.toFixed(1)} to ${transform.max.toFixed(1)}; gain ${view.gain}; ${samples.length} samples${clipped?'; clipped':''}`);
    };
    frame=requestAnimationFrame(render);return()=>cancelAnimationFrame(frame);
  },[store]);
  return <canvas ref={canvas} className="waveform" role="img" aria-label="ECG waveform in corrected ADC counts"/>;
}
