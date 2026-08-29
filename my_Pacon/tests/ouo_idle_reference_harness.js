// Offline Canvas playback of the actual preview script and generated data.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const {createCanvas,GlobalFonts}=require('@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyh.ttc','QA');
const root=path.join(__dirname,'..'),preview=path.join(root,'tools/ouo-preview');
const canvas=createCanvas(466,466);canvas.style={};
canvas.getBoundingClientRect=()=>({left:0,top:0,width:466,height:466});canvas.setPointerCapture=()=>{};
function element(){return {style:{},dataset:{},value:'',textContent:'',classList:{add(){},toggle(){}},
  append(){},querySelector:element,querySelectorAll:()=>[]};}
const nodes={'#face':canvas};let clock=1000,rafId=0;const callbacks=new Map();
const sandbox=vm.createContext({console,Math,Date,Promise,Uint8Array,
  atob:s=>Buffer.from(s,'base64').toString('binary'),
  document:{querySelector:s=>nodes[s]||(nodes[s]=element()),querySelectorAll:()=>[],
    createElement:t=>t==='canvas'?createCanvas(96,268):element()},
  performance:{now:()=>clock},navigator:{clipboard:{writeText(){}}},
  requestAnimationFrame:cb=>{callbacks.set(++rafId,cb);return rafId},cancelAnimationFrame:id=>callbacks.delete(id),
  setTimeout:()=>1,clearTimeout(){},setInterval:()=>1,clearInterval(){}});
vm.runInContext(fs.readFileSync(path.join(preview,'idle-reference.js'),'utf8'),sandbox);
vm.runInContext(fs.readFileSync(path.join(preview,'index.html'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1],sandbox);
const data=vm.runInContext('OUO_IDLE_REFERENCE',sandbox),bytes=Buffer.from(data.data,'base64');
const header=fs.readFileSync(path.join(root,'main/ouo_idle_reference.h'),'utf8');
const cBytes=Buffer.from(header.match(/ouo_idle_rle\[\] = \{([\s\S]*?)\};/)[1].match(/\d+/g).map(Number));
assert(bytes.equals(cBytes),'firmware and preview pixel bytes differ');
assert.equal(JSON.stringify(header.match(/ouo_idle_offsets\[\] = \{([^}]+)\}/)[1].match(/\d+/g).map(Number)),JSON.stringify(data.offsets));
const cEvents=header.match(/ouo_idle_events\[\]\[2\] = \{([^;]+)\};/)[1].match(/\d+/g).map(Number);
assert.equal(JSON.stringify(cEvents),JSON.stringify(data.timeline.flat()),'firmware timeline differs');
assert(header.includes(`#define OUO_IDLE_DURATION_MS ${data.durationMs}U`));
for(let frame=0;frame<data.offsets.length-1;++frame){
  let pixels=0;
  for(let i=data.offsets[frame];i<data.offsets[frame+1];i+=2){
    assert(bytes[i]>0);assert(bytes[i+1]<=15);pixels+=bytes[i];
  }
  assert.equal(pixels,96*268,`frame ${frame}: wrong decoded size`);
}
for(let i=0;i<data.timeline.length;++i){
  const [ms,frame]=data.timeline[i];
  if(i)assert(ms>data.timeline[i-1][0]);
  assert.equal(vm.runInContext(`idleFrameAt(${ms})`,sandbox),frame);
  if(i)assert.equal(vm.runInContext(`idleFrameAt(${ms-1})`,sandbox),data.timeline[i-1][1]);
}
assert.equal(vm.runInContext(`idleFrameAt(${data.durationMs})`,sandbox),data.timeline[0][1]);
nodes['#animate'].onclick();assert.equal(vm.runInContext('idlePlaying',sandbox),true);
assert.equal(vm.runInContext('updateIdleFrame(0)',sandbox),false,'a hold should not decode again');
const samples=[0,3000,4000,8000,15000,30000,34000,40000,58000];
const sheet=createCanvas(960,1008),ctx=sheet.getContext('2d');ctx.fillStyle='#20232a';ctx.fillRect(0,0,960,1008);
samples.forEach((ms,i)=>{
  vm.runInContext(`updateIdleFrame(${ms});draw()`,sandbox);
  const x=i%3*320,y=Math.floor(i/3)*336;
  ctx.save();ctx.beginPath();ctx.arc(x+160,y+150,140,0,Math.PI*2);ctx.clip();
  ctx.drawImage(canvas,x+20,y+10,280,280);ctx.restore();
  ctx.fillStyle='#ddd';ctx.font='17px QA';ctx.textAlign='center';ctx.fillText(`录像待机 · ${(ms/1000).toFixed(1)} 秒`,x+160,y+317);
});
const output=path.join(preview,'experiments/idle-reference');fs.mkdirSync(output,{recursive:true});
fs.writeFileSync(path.join(output,'preview-timeline.png'),sheet.toBuffer('image/png'));
canvas.onpointerdown({clientX:233,clientY:264,pointerId:1});
assert.equal(vm.runInContext('idlePlaying',sandbox),false,'touch must stop passive playback');
assert.equal(vm.runInContext('state',sandbox),'kiss');
canvas.onpointermove({clientX:253,clientY:264,pointerId:1});
assert.equal(vm.runInContext('drag.mouth',sandbox),true,'mouth drag must still be armed');
canvas.onpointerup({pointerId:1});
nodes['#animate'].onclick();vm.runInContext("setState('sad')",sandbox);
assert.equal(vm.runInContext('idlePlaying',sandbox),false,'manual expression must stop playback');
nodes['#animate'].onclick();nodes['#animate'].onclick();
assert.equal(vm.runInContext('idlePlaying',sandbox),false,'stop button must stop playback');
const firmware=fs.readFileSync(path.join(root,'main/fluid_pendant.c'),'utf8');
assert(firmware.includes('ouo_idle_step(now);'));
assert(firmware.includes('!s_ouo_auto_expressions || s_ouo_touch_active'));
assert(!firmware.includes('s_ouo_next_blink_us'),'obsolete passive blink scheduler remains');
assert(firmware.includes('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT'));
console.log(`PASS: ${data.offsets.length-1} frames, ${data.timeline.length} event boundaries, C/JS data parity, loop/holds and gesture interruption.`);
