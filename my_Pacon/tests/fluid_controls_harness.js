// Executes mechanically translated C render bodies. Counts actual pixel work
// and transfer area; deliberately NOT an ESP32 latency benchmark.
const fs=require('fs'), path=require('path'), vm=require('vm'), assert=require('assert/strict');
const root=path.join(__dirname,'..');
const source=fs.readFileSync(path.join(root,'main/fluid_pendant.c'),'utf8');
function body(c,name) {
  const match=new RegExp('static (?:void|bool|uint16_t|int) '+name+'\\([^)]*\\)\\s*\\{').exec(c);
  assert(match,name); let end=match.index+match[0].length, start=end,depth=1;
  while(depth) {if(c[end]==='{')depth++;if(c[end]==='}')depth--;end++;}
  return c.slice(start,end-1);
}
function translate(s) {
  return s.replace(/uint16_t \*line = s_lcd_stripe \+ ([^;]+);/g,'const line=s_lcd_stripe.subarray($1);')
    .replace(/\(size_t\)/g,'').replace(/\bNULL\b/g,'null')
    .replace(/\bconst (?:int|bool|uint16_t|int64_t|esp_err_t) /g,'const ')
    .replace(/\b(?:int|bool|uint16_t|int64_t|esp_err_t) /g,'let ');
}
function runLegacy(c,name) {
  let pixels=0, transferred=0;
  const ctx=vm.createContext({LCD_WIDTH:475,LCD_HEIGHT:466,LCD_STRIPE_LINES:64,
    s_lcd_panel:{},s_lcd_done:{},s_lcd_stripe:new Uint16Array(475*64),
    ESP_OK:0,pdTRUE:true,TAG:'test',
    clamp_int:(v,a,b)=>Math.max(a,Math.min(b,v)),rgb565_for_sh8601:v=>v,
    fluid_settings_pixel(){pixels++;return 0;},fluid_colour_picker_pixel(){pixels++;return 0;},
    esp_lcd_panel_draw_bitmap(panel,x1,y1,x2,y2){transferred+=(x2-x1)*(y2-y1);return 0;},
    pdMS_TO_TICKS:v=>v,xSemaphoreTake:()=>true,ESP_LOGE(){throw Error('DMA failure');}
  });
  vm.runInContext(`function render(){${translate(body(c,name))}}`,ctx);
  ctx.render();pixels=0;transferred=0;ctx.render();
  return {pixels,transferred};
}
if (!source.includes('fluid_controls_render_band')) {
  const settings=runLegacy(source,'render_fluid_settings_frame');
  const picker=runLegacy(source,'render_fluid_colour_picker_frame');
  console.log('Warm change work baseline:',{settings,picker});
  assert(picker.pixels<=338 && picker.transferred<=12350,
    'Warm colour selection rebuilds the static wheel and transfers a full screen instead of only two marker bands');
  process.exit(0);
}
const before=fs.readFileSync(path.join(__dirname,'fixtures/fluid_controls_before.c'),'utf8');
function rasterTranslate(s) {
  s=s.replace(/\/\*[\s\S]*?\*\//g,'').replace(/\b(\d+\.\d+)f\b/g,'$1')
    .replace(/\(float\)|\(size_t\)|\(void\)/g,'').replace(/\bNULL\b/g,'null')
    .replace(/const int (\w+)\[\] = \{([^}]+)\};/g,'const $1=[$2];')
    .replace(/const liquid_palette_t \*palette = active_palette\(\);/g,'const palette=active_palette();')
    .replace(/palette->/g,'palette.').replace(/&lv_font_(\w+)/g,'"$1"')
    .replace(/hsv_to_rgb\(([\s\S]*?),\s*&red,\s*&green,\s*&blue\);/g,'[red,green,blue] = hsv_to_rgb($1);')
    .replace(/\b(const )?int (\w+) = ([^;]+);/g,(_,con,n,e)=>`${con?'const':'let'} ${n}=Math.trunc(${e});`)
    .replace(/\(int\)s_fluid_shape/g,'s_fluid_shape')
    .replace(/\bconst (?:float|bool|uint8_t|uint16_t|int64_t|uint32_t|double) /g,'const ')
    .replace(/\b(?:float|bool|uint8_t|uint16_t|int64_t|uint32_t|double) /g,'let ');
  // Preserve C truncation for explicit casts around trig results.
  while(s.includes('(int)(')) {
    const at=s.indexOf('(int)('),start=at+6;let end=start,depth=1;
    while(depth){if(s[end]==='(')depth++;if(s[end]===')')depth--;end++;}
    s=s.slice(0,at)+'Math.trunc('+s.slice(start,end-1)+')'+s.slice(end);
  }
  return s;
}
const signatures={fluid_settings_pixel:'x,y',fluid_colour_wheel_pixel:'dx,dy',fluid_colour_picker_pixel:'x,y',
  fluid_controls_render_band:'y1,y2,picker',fluid_colour_restore_marker:'cx,cy',fluid_colour_draw_marker:'cx,cy',
  render_fluid_settings_frame:'',render_fluid_colour_picker_frame:'',
  fluid_colour_picker_select:'x,y',fluid_colour_picker_touch:'x,y',
  fluid_queue_preferences:'',fluid_service_preferences:''};
let calls=0, pixels=0,bandPixels=0,bands=[],saves=0,failSave=false,failFlush=false,now=10000;
function rgb565(r,g,b){return ((r&248)<<8)|((g&252)<<3)|(b>>3);}
function hsv(h,s,v){
  h-=Math.floor(h);s=Math.max(0,Math.min(1,s));const chroma=v*s,sector=h*6;
  const mid=chroma*(1-Math.abs(sector%2-1)),m=v-chroma;
  const rgb=sector<1?[chroma,mid,0]:sector<2?[mid,chroma,0]:sector<3?[0,chroma,mid]:
    sector<4?[0,mid,chroma]:sector<5?[mid,0,chroma]:[chroma,0,mid];
  return rgb.map(x=>Math.trunc((x+m)*255+.5));
}
const ctx=vm.createContext({Math,Uint16Array,LCD_WIDTH:475,LCD_HEIGHT:466,
  UI_SCREEN_HOME:0,UI_SCREEN_FLUID_SETTINGS:1,UI_SCREEN_COLOUR_PICKER:2,
  FLUID_SHAPE_SIMPLE:0,FLUID_SHAPE_BLOCKS:1,FLUID_SHAPE_MATRIX:2,
  s_lcd_canvas:new Uint16Array(475*466),s_fluid_controls_canvas_screen:0,s_fluid_settings_drawn_shape:-1,
  s_colour_marker_drawn_x:0,s_colour_marker_drawn_y:0,s_colour_hue:.54,s_colour_saturation:.86,
  s_colour_wheel_cache:null,s_fluid_shape:0,s_settings_dirty:true,s_colour_picker_dirty:true,
  s_custom_palette_active:false,s_fluid_preferences_pending:false,s_fluid_preferences_retry_us:0,
  s_fluid_controls_input_us:0,s_fluid_ui_perf:{save_us:0,saves:0},s_touch_down:false,s_colour_dragging:false,
  s_touch_blocked_until_release:false,s_ui_screen:2,s_fluid_canvas_valid:true,
  s_fluid_controls_transfer_pixels:0,abs:Math.abs,atan2f:Math.atan2,sqrtf:Math.sqrt,sinf:Math.sin,cosf:Math.cos,
  hsv_to_rgb:hsv,rgb565,rgb565_blend:(a,b,alpha)=>alpha===255?b:a,
  active_palette:()=>({body:[31,169,237]}),
  home_in_circle:(x,y,cx,cy,r)=>(x-cx)**2+(y-cy)**2<=r*r,
  // Fonts and rounded-rectangle primitive are unchanged external boundaries;
  // shared deterministic stubs isolate whether the patch changes any pixels.
  home_in_round_rect:(x,y,x1,y1,x2,y2,r)=>x>=x1&&x<=x2&&y>=y1&&y<=y2,
  home_font_alpha:(x,y,l,t,font,text)=>y>=t&&y<t+18&&x>=l&&x<l+text.length*9?255:0,
  fluid_colour_prepare_cache(){},esp_timer_get_time:()=>now,
  fluid_controls_flush_band(y1,y2){
    assert(y1>=0&&y2<=466&&y1<y2,'in-screen band');
    bands.push([y1,y2]);bandPixels+=475*(y2-y1);return !failFlush;
  },fluid_controls_record_frame(){ctx.s_fluid_controls_input_us=0;},
  fluid_save_preferences(){saves++;return !failSave;},
  block_touch_until_release(){ctx.s_touch_blocked_until_release=true;ctx.s_touch_down=true;},
  set_custom_palette(h,s){ctx.s_colour_hue=h;ctx.s_colour_saturation=s;ctx.s_custom_palette_active=true;}
});
for(const [name,args] of Object.entries(signatures)) {
  const code=`function ${name}(${args}){${rasterTranslate(body(source,name))}}`;
  try{vm.runInContext(code,ctx);}catch(e){console.error(code);throw e;}
}
vm.runInContext(`function oldPicker(x,y){${rasterTranslate(body(before,'fluid_colour_picker_pixel'))}}
function oldSettings(x,y){${rasterTranslate(body(before,'fluid_settings_pixel'))}}`,ctx);
const staticPixel=ctx.fluid_colour_picker_pixel, settingsPixel=ctx.fluid_settings_pixel;
ctx.fluid_colour_picker_pixel=(x,y)=>{pixels++;return staticPixel(x,y);};
ctx.fluid_settings_pixel=(x,y)=>{pixels++;return settingsPixel(x,y);};
const wheelPixel=ctx.fluid_colour_wheel_pixel;
ctx.fluid_colour_wheel_pixel=(x,y)=>{calls++;return wheelPixel(x,y);};
function reset(){calls=pixels=bandPixels=0;bands=[];}
function compare(fn,label){
  const canvas=ctx.s_lcd_canvas;
  for(let y=0;y<466;y++)for(let x=0;x<475;x++)assert.equal(canvas[y*475+x],fn(x,y),`${label} ${x}/${y}`);
}
// Cold draw then every shape pair on the same page: compare full canvas, not
// just the regions the new implementation elects to repaint.
ctx.render_fluid_settings_frame();compare(ctx.oldSettings,'settings cold');
for(const shape of [1,2,0,2,1,0,0]) {
  reset();const previous=ctx.s_fluid_shape;ctx.s_fluid_shape=shape;ctx.render_fluid_settings_frame();
  assert(pixels<=57000&&bandPixels<=57000,'settings only redraws changed rows');
  if(previous===shape)assert.equal(bandPixels,0);
  compare(ctx.oldSettings,`settings ${shape}`);
}
// Cache values use the actual new C wheel formula, compared to the old formula
// via full-frame differential. Allocation and DMA are target-only boundaries.
const cache=new Uint16Array(301*301);
for(let y=-150;y<=150;y++)for(let x=-150;x<=150;x++)if(x*x+y*y<=22500)cache[(y+150)*301+x+150]=wheelPixel(x,y);
ctx.s_colour_wheel_cache=cache;
ctx.render_fluid_colour_picker_frame();compare(ctx.oldPicker,'picker cold');
let warmMax=0;
for(const [h,s] of [[0,1],[.25,1],[.5,1],[.75,1],[.99,1],[.54,.86],[.1,.15],[.2,.15],[.2,.15],[.7,.5]]) {
  ctx.s_colour_hue=h;ctx.s_colour_saturation=s;reset();ctx.render_fluid_colour_picker_frame();
  assert.equal(calls,0,'cached selection must not recompute wheel HSV/trig');
  assert(pixels<=169&&bandPixels<=12350,'marker-only warm update');warmMax=Math.max(warmMax,bandPixels);
  compare(ctx.oldPicker,`picker ${h}/${s}`);
}
// Low-memory fallback stays identical and still only restores 13x13 pixels.
ctx.s_colour_wheel_cache=null;ctx.s_colour_hue=.33;reset();ctx.render_fluid_colour_picker_frame();
assert(calls<=169&&pixels<=169);compare(ctx.oldPicker,'allocation fallback');
// Transfer failure must not claim a complete frame; next attempt is full.
failFlush=true;ctx.s_colour_hue=.4;ctx.s_colour_picker_dirty=true;ctx.render_fluid_colour_picker_frame();
assert(ctx.s_colour_picker_dirty);assert.equal(ctx.s_fluid_controls_canvas_screen,0);
failFlush=false;reset();ctx.render_fluid_colour_picker_frame();assert.equal(bandPixels,221350);compare(ctx.oldPicker,'DMA retry');
// Screen replacement / wake invalidation requires full redraw, including colour
// preview on return to settings, never stale pixels from another shared user.
ctx.s_fluid_controls_canvas_screen=0;ctx.s_lcd_canvas.fill(1234);reset();ctx.render_fluid_settings_frame();
assert.equal(bandPixels,221350);compare(ctx.oldSettings,'screen/wake return');
// Real colour touch handler: press, held movement, leave/re-enter, and header.
ctx.s_touch_down=false;ctx.s_ui_screen=2;ctx.fluid_colour_picker_touch(350,270);
const hue=ctx.s_colour_hue;ctx.fluid_colour_picker_touch(237,390);
assert.notEqual(ctx.s_colour_hue,hue,'held drag updates colour');
ctx.fluid_service_preferences();assert.equal(saves,0,'no flash writes while dragging');
ctx.fluid_colour_picker_touch(86,73);assert.equal(ctx.s_ui_screen,2,'drag across back does not navigate');
ctx.fluid_colour_picker_touch(130,270);ctx.s_touch_down=false;ctx.fluid_service_preferences();
assert.equal(saves,1,'release coalesces all colours into one save');assert(!ctx.s_fluid_preferences_pending);
ctx.fluid_service_preferences();assert.equal(saves,1,'no redundant release save');
ctx.s_touch_down=false;ctx.fluid_colour_picker_touch(20,450);assert(!ctx.s_colour_dragging);
const oldHue=ctx.s_colour_hue;ctx.fluid_colour_picker_touch(237,390);assert.equal(ctx.s_colour_hue,oldHue,'outside start cannot drag');
ctx.s_touch_down=false;ctx.fluid_colour_picker_touch(86,73);assert.equal(ctx.s_ui_screen,1);assert(ctx.s_touch_blocked_until_release);
// NVS failures stay pending with a 1-second retry interval, including page exit.
ctx.s_touch_down=false;ctx.s_ui_screen=0;ctx.fluid_queue_preferences();failSave=true;ctx.fluid_service_preferences();
assert(ctx.s_fluid_preferences_pending);const failedSaves=saves;ctx.fluid_service_preferences();assert.equal(saves,failedSaves);
now+=1000001;failSave=false;ctx.fluid_service_preferences();assert(!ctx.s_fluid_preferences_pending);
console.log(`PASS: full-frame old/new differentials; warm settings <=57000 pixels; warm picker <=169 evaluations / ${warmMax} transferred pixels; continuous drag, release save, allocation fallback and DMA/NVS failure recovery.`);
