// Run a mechanical JS translation of the two small integer C raster helpers.
// No duplicate implementation of their bounds/compositing algorithms. This
// checks pixel equivalence, not ESP32 execution speed; target timing is serial.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const root=path.join(__dirname,'..'),c=fs.readFileSync(path.join(root,'main/fluid_pendant.c'),'utf8');
const box=vm.createContext({console,Uint8Array,Uint16Array});
vm.runInContext(fs.readFileSync(path.join(root,'tools/ouo-preview/idle-reference.js'),'utf8'),box);
const data=vm.runInContext('OUO_IDLE_REFERENCE',box),rle=Buffer.from(data.data,'base64');
function body(name){const start=c.indexOf(`static ${name==='ouo_idle_pixel'?'uint16_t':'void'} ${name}(`);assert(start>=0);const first=c.indexOf('{',start);let depth=1,end=first+1;while(depth){if(c[end]==='{')depth++;if(c[end]==='}')depth--;end++;}return c.slice(first+1,end-1);}
function translate(text){
  return text.replace(/const int (\w+)\[3\] = \{([\s\S]*?)\};/g,'const $1 = [$2];')
    .replace(/const uint8_t \*source = s_ouo_idle_tiles \+ ([^;]+);/g,'const source = s_ouo_idle_tiles.subarray($1);')
    .replace(/uint16_t \*target = s_lcd_canvas \+ ([^;]+);/g,'const target = s_lcd_canvas.subarray($1);')
    .replace(/\(size_t\)/g,'').replace(/dirty->/g,'dirty.')
    .replace(/const (?:int|uint8_t|uint16_t) /g,'const ').replace(/\b(?:int|uint8_t|uint16_t) /g,'let ');
}
const constants=['LCD_WIDTH','LCD_HEIGHT','OUO_LEFT_EYE_X','OUO_RIGHT_EYE_X','OUO_EYE_Y','OUO_MOUTH_Y'];
let declarations=constants.map(key=>`const ${key}=${c.match(new RegExp('#define '+key+'\\s+(\\d+)'))[1]};`).join('\n');
declarations+='const OUO_FACE_CENTER_X=(OUO_LEFT_EYE_X+OUO_RIGHT_EYE_X)/2;';
vm.runInContext(`${declarations}
let s_ouo_gaze_x=0,s_ouo_gaze_y=0,s_ouo_shake_x=0,s_ouo_shake_y=0;
let s_ouo_idle_palette_ready=false;
const s_ouo_idle_palette=new Uint16Array(256),s_ouo_idle_tiles=new Uint8Array(96*268);
const s_lcd_canvas=new Uint16Array(LCD_WIDTH*LCD_HEIGHT);
function rgb565(r,g,b){return ((r&248)<<8)|((g&252)<<3)|(b>>3)}
function oldPixel(x,y){${translate(body('ouo_idle_pixel'))}}
function blit(dirty){${translate(body('ouo_blit_idle_tiles'))}}
function oldFrame(dx,dy){const out=new Uint16Array(LCD_WIDTH*LCD_HEIGHT);for(let y=112;y<370;y++)for(let x=0;x<LCD_WIDTH;x++)out[y*LCD_WIDTH+x]=oldPixel(x-dx,y-dy);return out}
function newFrame(dx,dy){s_ouo_gaze_x=dx;s_ouo_gaze_y=dy;s_lcd_canvas.fill(0);blit({x1:0,y1:112,x2:LCD_WIDTH,y2:370});return s_lcd_canvas}
`,box);
const tiles=vm.runInContext('s_ouo_idle_tiles',box),oldFrame=vm.runInContext('oldFrame',box),newFrame=vm.runInContext('newFrame',box);
let checked=0;
for(let frame=0;frame<data.offsets.length-1;frame++){
  let pos=0;for(let i=data.offsets[frame];i<data.offsets[frame+1];i+=2){tiles.fill(rle[i+1]*17,pos,pos+rle[i]);pos+=rle[i]}
  const shifts=frame%64===0?[[0,0],[-78,-47],[78,47],[-250,-200],[250,240]]:[[0,0]];
  for(const [x,y] of shifts){const expected=oldFrame(x,y),actual=newFrame(x,y);assert(Buffer.from(expected.buffer).equals(Buffer.from(actual.buffer)),`pixel mismatch frame ${frame}, shift ${x}/${y}`);checked++;}
}
const events=data.timeline;let short=0;for(let i=1;i<events.length;i++)if(events[i][0]-events[i-1][0]<=50)short++;
console.log(`PASS: ${checked} full-canvas comparisons, including ${data.offsets.length-1} source frames and extreme clipping. ${short}/${events.length-1} timeline intervals <=50ms; source timing unchanged.`);
