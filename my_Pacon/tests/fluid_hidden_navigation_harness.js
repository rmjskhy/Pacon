// Verify the real touch branch predicates, not a separate hit-test replica.
const fs = require('fs'), path = require('path'), assert = require('assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../main/fluid_pendant.c'), 'utf8');
assert(!source.includes('render_home_control_into_canvas'), 'No button painting on full or dirty redraw');
assert(!source.includes('s_fluid_controls_visible'), 'No hidden-control first-press interception');
assert(!source.includes('FLUID_CONTROL_TIMEOUT_MS'), 'No obsolete visibility timeout');
const poll = source.slice(source.indexOf('static void poll_touch(void)'));
const featureBounds = Object.fromEntries(['X1','X2','Y1','Y2'].map(axis => {
  const match = source.match(new RegExp('#define FEATURE_SETTINGS_'+axis+'\\s+(\\d+)'));
  assert(match, 'Missing feature settings '+axis);
  return ['FEATURE_SETTINGS_'+axis, Number(match[1])];
}));
function predicate(destination) {
  const match = poll.match(new RegExp('else if \\(([^)]+)\\) \\{\\s+s_ui_screen = '+destination+';'));
  assert(match, 'Missing fluid navigation branch '+destination);
  assert(match[1].includes('x') && match[1].includes('y'));
  let expression=match[1];
  for(const [name,value] of Object.entries(featureBounds))expression=expression.replaceAll(name,String(value));
  return new Function('x', 'y', 'return '+expression);
}
const home = predicate('UI_SCREEN_HOME'), settings = predicate('UI_SCREEN_FLUID_SETTINGS');
let homeCount=0, settingsCount=0;
for (let y=0; y<466; ++y) for (let x=0; x<475; ++x) {
  assert.equal(home(x,y), y>=68 && y<125 && x>=55 && x<215, `Home at ${x}/${y}`);
  assert.equal(settings(x,y), x>=326 && x<448 && y>=24 && y<132, `Settings at ${x}/${y}`);
  homeCount+=Number(home(x,y)); settingsCount+=Number(settings(x,y));
}
assert(poll.includes('if (!s_touch_down) {'), 'Navigation remains press-edge triggered');
assert(poll.includes('s_touch_energy = 255;'), 'Ordinary touches retain liquid impulse');
console.log(`PASS: invisible fluid navigation; home ${homeCount} pixels, settings ${settingsCount} pixels in shared upper-right region.`);
