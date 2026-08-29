// Execute the actual small C codec/store bodies after mechanical syntax
// translation, with an in-memory NVS boundary. This is not a target power-cut
// test; firmware compilation separately checks native types/linkage.
const fs=require('fs'), path=require('path'), vm=require('vm'), assert=require('assert/strict');
const root=path.join(__dirname,'..');
const c=fs.readFileSync(path.join(root,'main/pacon_preferences.c'),'utf8');
function body(name) {
  const match=new RegExp('(?:static )?(?:bool|void|esp_err_t) '+name+'\\([^)]*\\)\\s*\\{').exec(c);
  assert(match, name);
  const start=match.index+match[0].length; let end=start, depth=1;
  while(depth) { if(c[end]==='{')depth++; if(c[end]==='}')depth--; end++; }
  return c.slice(start,end-1);
}
function translate(s) {
  return s.replace(/const uint8_t (\w+)\[\d+\] = \{([\s\S]*?)\};/g,'const $1=Uint8Array.from([$2]);')
    .replace(/uint8_t (\w+)\[(\d+)\];/g,'const $1=new Uint8Array($2);')
    .replace(/nvs_handle_t handle;/g,'const handle={};')
    .replace(/err = nvs_get_blob\(handle, key, bytes, &size\);/g,'[err,size] = nvs_get_blob(handle,key,bytes,size);')
    .replace(/&handle/g,'handle').replace(/\(uint16_t\)/g,'').replace(/value->/g,'value.')
    .replace(/sizeof\((\w+)\)/g,'$1.length')
    .replace(/const (?:uint16_t|esp_err_t|size_t) /g,'const ')
    .replace(/\b(?:uint16_t|esp_err_t|size_t) /g,'let ');
}
const signatures={read_record:'key,bytes,expected',write_record:'key,bytes,size',
  pacon_load_fluid_preferences:'value',pacon_save_fluid_preferences:'value',
  pacon_load_ouo_preferences:'value',pacon_save_ouo_preferences:'value',
  pacon_load_switch:'key,fallback',pacon_save_switch:'key,value'};
const code=Object.entries(signatures).map(([n,args])=>`function ${n}(${args}){${translate(body(n))}}`).join('\n');
const disk=new Map(); let writes=0, failure='';
function boot() {
  const context=vm.createContext({Uint8Array, ESP_OK:0,ESP_ERR_NVS_NOT_FOUND:1,
    ESP_ERR_INVALID_SIZE:2,ESP_ERR_INVALID_ARG:3,PREFS_NAMESPACE:'pacon_prefs',TAG:'test',
    NVS_READONLY:0,NVS_READWRITE:1,ESP_LOGW(){},ESP_LOGE(){},esp_err_to_name: n=>String(n),
    memcmp:(a,b,n)=>Buffer.from(a).subarray(0,n).equals(Buffer.from(b).subarray(0,n))?0:1,
    nvs_open(ns,mode,h){assert.equal(ns,'pacon_prefs');h.mode=mode;return failure==='open'?9:0;},
    nvs_close(h){h.pending=undefined;},
    nvs_get_blob(h,key,bytes,size){if(!disk.has(key))return [1,size];const b=disk.get(key);
      if(b.length>size)return [2,b.length]; bytes.set(b);return [0,b.length];},
    nvs_set_blob(h,key,bytes,size){assert.equal(h.mode,1);if(failure==='set')return 9;
      h.pending=[key,Buffer.from(bytes).subarray(0,size)];return 0;},
    nvs_commit(h){if(failure==='commit')return 9;assert(h.pending);disk.set(...h.pending);writes++;return 0;}
  });
  vm.runInContext(code,context); return context;
}
let api=boot();
function fluidDefault(){return {shape:0,custom:false,hue:5400,saturation:8600};}
function ouoDefault(){return {automatic:true,tilt:true,mood:68};}
let f=fluidDefault(),o=ouoDefault();api.pacon_load_fluid_preferences(f);api.pacon_load_ouo_preferences(o);
assert.deepEqual(f,fluidDefault());assert.deepEqual(o,ouoDefault());
let cases=0;
for(const shape of [0,1,2])for(const custom of [false,true])for(const hue of [0,5400,9999])for(const saturation of [1500,8600,10000]){
  const expected={shape,custom,hue,saturation};assert.equal(api.pacon_save_fluid_preferences(expected),0);
  api=boot();const actual=fluidDefault();api.pacon_load_fluid_preferences(actual);assert.deepEqual(actual,expected);cases++;
  const before=writes;assert.equal(api.pacon_save_fluid_preferences(expected),0);assert.equal(writes,before);
}
for(const automatic of [false,true])for(const tilt of [false,true])for(const mood of [0,33,68,100]){
  const expected={automatic,tilt,mood};assert.equal(api.pacon_save_ouo_preferences(expected),0);
  api=boot();const actual=ouoDefault();api.pacon_load_ouo_preferences(actual);assert.deepEqual(actual,expected);cases++;
}
for(const key of ['wifi_on','wifi_join','ble_on'])for(const value of [false,true]){
  assert.equal(api.pacon_save_switch(key,value),0);api=boot();assert.equal(api.pacon_load_switch(key,!value),value);cases++;
}
for(const bytes of [[2,0,0,0,0,33,152],[1,3,0,0,0,33,152],[1,0,2,0,0,33,152],
  [1,0,1,255,255,33,152],[1,0,1,0,0,0,0],[1,0,1,0,0,255,255],[1,0],[1,0,0,0,0,33,152,0]]){
  disk.set('fluid',Buffer.from(bytes));const actual=fluidDefault();boot().pacon_load_fluid_preferences(actual);assert.deepEqual(actual,fluidDefault());cases++;
}
for(const bytes of [[2,1,1,68],[1,2,1,68],[1,1,2,68],[1,1,1,101],[1]]){
  disk.set('ouo',Buffer.from(bytes));const actual=ouoDefault();boot().pacon_load_ouo_preferences(actual);assert.deepEqual(actual,ouoDefault());cases++;
}
disk.set('ble_on',Buffer.from([1,2]));assert.equal(boot().pacon_load_switch('ble_on',false),false);
for(const stage of ['open','set','commit']){
  failure=stage;const before=writes;
  assert.equal(boot().pacon_save_switch('test_failure',true),9);assert.equal(writes,before);cases++;
}
failure='';
assert.equal(api.pacon_save_fluid_preferences({...fluidDefault(),shape:3}),3);
assert.equal(api.pacon_save_ouo_preferences({...ouoDefault(),mood:101}),3);
console.log(`PASS: ${cases} settings restore / invalid-data / failure scenarios; duplicate writes suppressed.`);
