// Exercise the actual pointer handlers with a deterministic clock and real
// raster canvas. No browser automation or static research-button shortcuts.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const {createCanvas,GlobalFonts}=require('@napi-rs/canvas');
GlobalFonts.registerFromPath('C:/Windows/Fonts/msyh.ttc','QA');
const root=path.join(__dirname,'..'),preview=path.join(root,'tools/ouo-preview');
const canvas=createCanvas(466,466);canvas.style={};canvas.setPointerCapture=()=>{};
canvas.getBoundingClientRect=()=>({left:0,top:0,width:466,height:466});
const element=()=>({style:{},dataset:{},value:'',textContent:'',classList:{add(){},toggle(){}},append(){},querySelector:element,querySelectorAll:()=>[]});
const nodes={'#face':canvas},timeouts=new Map(),rafs=new Map();let clock=1000,id=0,random=.9;
const box=vm.createContext({console,Date,Uint8Array,Promise,
  Math:Object.assign(Object.create(Math),{random:()=>random}),
  atob:s=>Buffer.from(s,'base64').toString('binary'),
  document:{querySelector:s=>nodes[s]||(nodes[s]=element()),querySelectorAll:()=>[],createElement:t=>t==='canvas'?createCanvas(96,268):element()},
  performance:{now:()=>clock},navigator:{clipboard:{writeText(){}}},
  requestAnimationFrame:f=>{rafs.set(++id,f);return id},cancelAnimationFrame:i=>rafs.delete(i),
  setTimeout:(f,ms)=>{timeouts.set(++id,{f,at:clock+ms});return id},clearTimeout:i=>timeouts.delete(i),
  setInterval:()=>++id,clearInterval(){}});
const run=s=>vm.runInContext(s,box);
run(fs.readFileSync(path.join(preview,'idle-reference.js'),'utf8'));
run(fs.readFileSync(path.join(preview,'index.html'),'utf8').match(/<script>([\s\S]*?)<\/script>/)[1]);
function advance(ms){const end=clock+ms;for(;;){const next=[...timeouts].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];if(!next)break;clock=next[1].at;timeouts.delete(next[0]);next[1].f()}clock=end;}
const down=(x,y,pointerId=1)=>canvas.onpointerdown({clientX:x,clientY:y,pointerId});
const move=(x,y,pointerId=1)=>canvas.onpointermove({clientX:x,clientY:y,pointerId});
const up=(pointerId=1)=>canvas.onpointerup({pointerId});
const samples=[];
function shot(label){run('draw()');const copy=createCanvas(466,466);copy.getContext('2d').putImageData(canvas.getContext('2d').getImageData(0,0,466,466),0,0);samples.push({label,canvas:copy});}

down(233,180);assert.equal(run('pointerKind'),'head');assert.equal(run('state'),'headPat');
advance(3200);move(253,185);assert.equal(run('state'),'headPat');shot('额头按住 3.2 秒');
down(346,216,2);move(300,260,2);up(2);assert.equal(run('state'),'headPat','second pointer must not steal the first');
up();assert.equal(run('state'),'delighted');shot('额头松开 · 灰色脸颊');
advance(5999);assert.equal(run('state'),'delighted');advance(1);assert.equal(run('state'),'idle');
down(120,216);advance(2000);move(122,214);assert.equal(run('state'),'winkLeft');shot('左眼按住 · 不变惊讶');
up();assert.equal(run('state'),'caret');advance(700);assert.equal(run('state'),'idle');
down(346,216);advance(1300);assert.equal(run('state'),'winkRight');shot('右眼按住 · 折线嘴');up();advance(700);

// Real downward gesture followed by a semicircle. Family must remain locked
// even with multiple large direction reversals in one short time window.
down(233,284);move(233,344);assert.equal(run('drag.mouthVariant'),'triangle');shot('三角嘴 · 竖拉');
move(303,344);shot('同一次手势 · 向右拉宽');
for(const [x,y] of [[163,344],[303,344],[163,344],[233,344],[233,284]])move(x,y);
assert.equal(run('state'),'surprised');assert.equal(run('drag.mouthVariant'),'triangle');
assert.equal(canvas.style.opacity,'1','drag must not repeatedly fade the whole face');
up();assert.equal(run('state'),'mouthRelease');shot('拉嘴松开 · 收成短横');advance(650);assert.equal(run('state'),'idle');
for(const sign of [-1,1]){down(233+sign*50,284);move(233+sign*90,284);assert.equal(run('drag.mouthVariant'),'side');shot(sign<0?'向左嘴侧拉':'向右嘴侧拉');up();advance(650);}

// Geometry invariants on rasterized triangles, not just path/control points.
function triangleBounds(dx,dy){
  run(`ctx.clearRect(0,0,466,466);ctx.save();ctx.translate(233,180);ctx.fillStyle='#fff';liveTrianglePullMouth(${dx},${dy},0,0);ctx.restore()`);
  const data=canvas.getContext('2d').getImageData(0,0,466,466).data;let x0=466,y0=466,x1=0,y1=0,area=0;
  for(let y=0;y<466;y++)for(let x=0;x<466;x++)if(data[(y*466+x)*4+3]>127){x0=Math.min(x0,x);y0=Math.min(y0,y);x1=Math.max(x1,x);y1=Math.max(y1,y);area++}
  return {w:x1-x0+1,h:y1-y0+1,x0,x1,y0,y1,area};
}
const short=triangleBounds(0,24),long=triangleBounds(0,84),wide=triangleBounds(84,84),left=triangleBounds(-84,84);
assert.equal(short.w,long.w,'vertical pulling must not widen the triangle');
assert(long.h>short.h+30);assert(wide.w>long.w+85);
// A subpixel narrow apex can lose two thresholded rows to antialiasing.
assert(Math.abs(wide.h-long.h)<=2);
assert.deepEqual(wide,left,'left/right deformation must preserve an upright, symmetric silhouette');

// Pending release and idle timers must not stomp a new gesture or selection.
nodes['#animate'].onclick();down(233,180);up();advance(100);down(346,216);advance(6500);
assert.equal(run('state'),'winkRight');assert.equal(run('idlePlaying'),false);
up();advance(700);assert.equal(run('idlePlaying'),true,'auto idle resumes only after release feedback');
down(233,180);up();run("setState('sad')");advance(7000);assert.equal(run('state'),'sad');
down(233,180);canvas.onpointercancel({pointerId:1});assert.equal(run('drag'),null);assert.equal(run('state'),'idle');

const c=fs.readFileSync(path.join(root,'main/fluid_pendant.c'),'utf8');
assert(c.includes('ouo_set_expression(OUO_EXPRESSION_DELIGHTED, 6000)'));
assert(c.includes('ouo_set_expression(OUO_EXPRESSION_CARET, 700)'));
assert(c.includes('const int half = 18 + ax * 48 / 84;'));
assert(c.includes('const int base = 14 + ay * 42 / 84;'));
assert(c.includes('if (s_ouo_expression != OUO_EXPRESSION_IDLE && !s_ouo_touch_active &&'));
assert(!c.includes('s_ouo_touch_started_us >= 650000LL'),'old eye-hold timeout remains');
const sheet=createCanvas(960,1008),ctx=sheet.getContext('2d');ctx.fillStyle='#20232a';ctx.fillRect(0,0,960,1008);
samples.forEach(({label,canvas},i)=>{const x=i%3*320,y=Math.floor(i/3)*336;ctx.save();ctx.beginPath();ctx.arc(x+160,y+150,140,0,Math.PI*2);ctx.clip();ctx.drawImage(canvas,x+20,y+10,280,280);ctx.restore();ctx.fillStyle='#ddd';ctx.font='16px QA';ctx.textAlign='center';ctx.fillText(label,x+160,y+317)});
const out=path.join(preview,'experiments/touch-recording');fs.mkdirSync(out,{recursive:true});
fs.writeFileSync(path.join(out,'gesture-checks.png'),sheet.toBuffer('image/png'));
samples.forEach((sample,i)=>fs.writeFileSync(path.join(out,`gesture-${i}.png`),sample.canvas.toBuffer('image/png')));
console.log('PASS: held forehead/eyes, release timers, pointer ownership, idle resume, live mirrored side pulls, triangle axis raster bounds.',{short,long,wide});
