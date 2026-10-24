import {useEffect,useRef} from 'react';
import {ECGStore} from './model';

export function Waveform({store,seconds,gain,paused=false,markers=true}:{store:ECGStore;seconds:number;gain:number;paused?:boolean;markers?:boolean}) {
  const canvas=useRef<HTMLCanvasElement>(null);
  useEffect(()=>{
    let frame=0,lastDraw=0;
    const frozen=paused?{samples:[...store.samples],events:[...store.events]}:null;
    const render=(now:number)=>{
      frame=requestAnimationFrame(render);if(now-lastDraw<32)return;lastDraw=now;
      const element=canvas.current;if(!element)return;
      const width=element.clientWidth,height=element.clientHeight,ratio=window.devicePixelRatio||1;
      if(element.width!==Math.round(width*ratio)||element.height!==Math.round(height*ratio)){element.width=Math.round(width*ratio);element.height=Math.round(height*ratio);}
      const ctx=element.getContext('2d');if(!ctx)return;
      ctx.setTransform(ratio,0,0,ratio,0,0);
      const style=getComputedStyle(element),grid=style.getPropertyValue('--grid'),ink=style.getPropertyValue('--muted');
      ctx.clearRect(0,0,width,height);
      ctx.strokeStyle=grid;ctx.lineWidth=.6;ctx.beginPath();
      const step=width/(seconds*5);
      for(let x=0;x<width;x+=step){ctx.moveTo(x,0);ctx.lineTo(x,height);}
      for(let y=0;y<height;y+=20){ctx.moveTo(0,y);ctx.lineTo(width,y);}ctx.stroke();
      const samples=frozen?.samples||store.samples,events=frozen?.events||store.events;
      if(!samples.length){ctx.fillStyle=ink;ctx.font='14px system-ui';ctx.textAlign='center';ctx.fillText('Connect a source and press START to receive ECG',width/2,height/2);return;}
      const end=samples.at(-1)!.t,begin=end-seconds*1000;
      const x=(t:number)=>(t-begin)/(seconds*1000)*width;
      const y=(v:number)=>height*.58-v*gain*height/2600;
      ctx.strokeStyle=style.getPropertyValue('--trace');ctx.lineWidth=1.7;ctx.lineJoin='round';ctx.beginPath();
      let started=false;
      for(const sample of samples){if(sample.t<begin){started=false;continue;}if(!started||sample.gap)ctx.moveTo(x(sample.t),y(sample.v));else ctx.lineTo(x(sample.t),y(sample.v));started=true;}ctx.stroke();
      if(markers){ctx.fillStyle='#ed4d65';for(const event of events){if(!event.r_peak||event.t<begin||event.t>end)continue;const px=x(event.t);ctx.beginPath();ctx.moveTo(px-4,14);ctx.lineTo(px+4,14);ctx.lineTo(px,21);ctx.fill();}}
      ctx.fillStyle=ink;ctx.font='11px ui-monospace, monospace';ctx.textAlign='left';
      ctx.fillText(`${(begin/1000).toFixed(1)} s`,8,height-9);ctx.textAlign='right';ctx.fillText(`${(end/1000).toFixed(1)} s`,width-8,height-9);
    };
    frame=requestAnimationFrame(render);return()=>cancelAnimationFrame(frame);
  },[store,seconds,gain,paused,markers]);
  return <canvas ref={canvas} className="waveform" role="img" aria-label="ECG waveform with device timestamp aligned R-peak markers"/>;
}
