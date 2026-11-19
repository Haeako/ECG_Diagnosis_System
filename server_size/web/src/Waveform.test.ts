import {describe,it,expect} from 'vitest';
import {coordinateTransform,estimateScale,markerSample,niceTicks,updateScale} from './Waveform';
import type {Sample} from './model';

describe('waveform coordinates and robust scale',()=>{
  it('uses one invertible transform for trace, ticks and gain at any center',()=>{
    for(const center of [-700,0,1234])for(const half of [50,500,1000,32768])for(const gain of [.5,1,2,4]){
      const tx=coordinateTransform(center,half,gain,30,200);
      expect(tx.y(center)).toBe(130);
      expect(tx.y(tx.min)).toBeCloseTo(230);expect(tx.y(tx.max)).toBeCloseTo(30);
      const base=coordinateTransform(center,half,1,30,200);
      expect(130-tx.y(center+25)).toBeCloseTo((130-base.y(center+25))*gain);
      for(const v of [center-123,center,center+432,...niceTicks(tx.min,tx.max)])expect(tx.value(tx.y(v))).toBeCloseTo(v);
    }
  });
  it('produces 5â€“7 increasing numeric nice ticks across shifted and zoomed ranges',()=>{
    for(const center of [-32000,-500.3,0,124.2,12000])for(const half of [12.5,50,100,137,258,1000,16800,131072]){
      const ticks=niceTicks(center-half,center+half);
      expect(ticks.length).toBeGreaterThanOrEqual(5);expect(ticks.length).toBeLessThanOrEqual(7);
      expect(ticks[0]).toBeGreaterThanOrEqual(center-half-1e-7);
      expect(ticks.at(-1)!).toBeLessThanOrEqual(center+half+1e-7);
      const delta=ticks[1]-ticks[0];
      for(let i=1;i<ticks.length;i++)expect(ticks[i]-ticks[i-1]).toBeCloseTo(delta);
    }
    expect(niceTicks(-1000,1000)).toEqual([-1000,-500,0,500,1000]);
  });
  it('centers offset ECG, ignores isolated outliers and retains normal QRS headroom without mutation',()=>{
    const samples:Sample[]=Array.from({length:1000},(_,i)=>({t:i*10,v:800+700*Math.exp(-Math.pow((i%100-40)/2,2))-150*Math.exp(-Math.pow((i%100-45)/2,2))}));
    const copy=structuredClone(samples),estimate=estimateScale(samples,true);
    expect(estimate.baseline).toBeCloseTo(800);
    for(const sample of samples)expect(Math.abs(sample.v-estimate.baseline)).toBeLessThan(estimate.half);
    samples[123]={t:1230,v:32000};samples[456]={t:4560,v:-32000};
    expect(estimateScale(samples,true).half).toBeCloseTo(estimate.half);
    expect(estimateScale(copy,false).half).toBeGreaterThan(estimate.half);
    expect(copy[123].v).not.toBe(32000);
    const before=structuredClone(samples);estimateScale(samples,true);expect(samples).toEqual(before);
  });
  it('handles empty, flat, invalid data and limits the range',()=>{
    expect(estimateScale([],true)).toEqual({baseline:0,half:100});
    expect(estimateScale(Array.from({length:100},(_,t)=>({t,v:900})),true)).toEqual({baseline:900,half:50});
    expect(estimateScale([{t:0,v:NaN},{t:NaN,v:3}],true)).toEqual({baseline:0,half:100});
    expect(estimateScale([{t:0,v:-1e6},{t:1,v:1e6}],true).half).toBe(65536);
  });
  it('uses growth headroom, a deadband and three fresh estimates before shrinking',()=>{
    let state=updateScale(undefined,{baseline:500,half:1000});
    for(const half of [950,1050,980,1090]){state=updateScale(state,{baseline:510,half});expect(state.half).toBe(1000);expect(state.targetHalf).toBe(1000);}
    expect(state.baseline).toBe(500);expect(state.targetBaseline).toBe(510);
    state=updateScale(state,{baseline:510,half:1500});expect(state.half).toBe(1500);
    for(let i=0;i<2;i++){state=updateScale(state,{baseline:510,half:500});expect(state.targetHalf).toBe(1500);}
    state=updateScale(state,{baseline:510,half:500});expect(state.targetHalf).toBe(500);expect(state.half).toBe(1500);
  });
  it('anchors markers to nearby samples only, never across gaps or outside coverage',()=>{
    const samples=[{t:100,v:2},{t:110,v:20},{t:120,v:5},{t:200,v:50,gap:true},{t:210,v:10}];
    expect(markerSample(samples,110)).toBe(samples[1]);
    expect(markerSample(samples,113)).toBe(samples[1]);
    for(const t of [90,160,195,200,220])expect(markerSample(samples,t)).toBeUndefined();
    expect(markerSample([],100)).toBeUndefined();
    expect(markerSample([{t:0,v:NaN}],0)).toBeUndefined();
    expect(markerSample([{t:0,v:1},{t:40,v:4}],20)).toBeUndefined();
  });
});
