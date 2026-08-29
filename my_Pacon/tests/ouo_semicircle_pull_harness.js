const fs = require('fs');
const vm = require('vm');

const html = fs.readFileSync(require('path').join(__dirname, '..', 'tools', 'ouo-preview', 'index.html'), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/i)?.[1];
if (!script) throw new Error('OuO preview script not found');

const commands = [];
let currentPath = [];
const buttons = [];
let rotateCount = 0;
const translations = [];
let randomValue = 0.3;
const scales = [];
const ctx = {
  beginPath: () => { currentPath = []; commands.push({ type: 'begin' }); },
  moveTo: (x, y) => { currentPath.push([x, y]); commands.push({ type: 'move', x, y }); },
  lineTo: (x, y) => { currentPath.push([x, y]); commands.push({ type: 'line', x, y }); },
  quadraticCurveTo: (cx, cy, x, y) => { currentPath.push([cx, cy], [x, y]); commands.push({ type: 'quad', cx, cy, x, y }); },
  bezierCurveTo: (c1x, c1y, c2x, c2y, x, y) => { currentPath.push([c1x, c1y], [c2x, c2y], [x, y]); commands.push({ type: 'bezier', c1x, c1y, c2x, c2y, x, y }); },
  closePath: () => commands.push({ type: 'close' }),
  fill: () => commands.push({ type: 'fill', points: currentPath.slice() }),
  stroke: () => commands.push({ type: 'stroke', points: currentPath.slice() }),
  ellipse: (x, y, rx, ry) => { currentPath.push([x - rx, y - ry], [x + rx, y + ry]); commands.push({ type: 'ellipse', x, y, rx, ry }); },
  arc: () => {}, rect: () => {}, clip: () => {}, save: () => {}, restore: () => {},
  translate: (x, y) => { translations.push([x, y]); }, scale: (x, y) => { scales.push([x, y]); }, rotate: () => { rotateCount++; }, fillRect: () => {},
};

function element(tag = 'div') {
  return {
    tagName: tag.toUpperCase(),
    style: {}, dataset: {}, value: '', textContent: '',
    classList: { add() {}, toggle() {} },
    append() {}, appendChild() {}, querySelector() { return element(); }, querySelectorAll() { return []; },
    addEventListener() {},
  };
}

const canvas = element();
canvas.width = 466; canvas.height = 466;
canvas.getContext = () => ctx;
canvas.getBoundingClientRect = () => ({ left: 0, top: 0, width: 466, height: 466 });
canvas.setPointerCapture = () => {};
const byId = { face: canvas, states: element(), controls: element(), export: element(), reset: element(), animate: element(), shot: element(), copy: element() };

const document = {
  querySelector(selector) { return byId[selector.slice(1)] || element(); },
  querySelectorAll(selector) { return selector === '[data-state]' ? buttons : []; },
  createElement(tag) { const e = element(tag); if (tag === 'button') buttons.push(e); return e; },
};
const context = {
  console, document, navigator: { clipboard: { writeText: async () => {} } },
  performance: { now: () => 1000 },
  requestAnimationFrame: fn => { fn(); return 1; }, cancelAnimationFrame() {},
  setTimeout: () => 1, clearTimeout() {}, setInterval: () => 1, clearInterval() {},
  Math: Object.assign(Object.create(Math), { random: () => randomValue }), Date, Promise,
};
vm.runInNewContext(script, context, { filename: 'ouo-preview/index.html' });

function mouthBounds() {
  const fills = commands.filter(c => c.type === 'fill' && c.points?.length);
  const points = fills.at(-1)?.points || [];
  if (!points.length) throw new Error('no filled mouth path captured');
  const xs = points.map(p => p[0]);
  const ys = points.map(p => p[1]);
  return {
    width: Math.max(...xs) - Math.min(...xs), height: Math.max(...ys) - Math.min(...ys),
    left: Math.min(...xs), right: Math.max(...xs), top: Math.min(...ys), bottom: Math.max(...ys),
  };
}

function compositeMouthBounds() {
  const strokes = commands.filter(c => c.type === 'stroke' && c.points?.length);
  const fills = commands.filter(c => c.type === 'fill' && c.points?.length);
  const points = [...strokes, fills.at(-1)].filter(Boolean).flatMap(c => c.points);
  if (!points.length) throw new Error('no composite mouth paths captured');
  const xs = points.map(p => p[0]);
  const ys = points.map(p => p[1]);
  return {
    width: Math.max(...xs) - Math.min(...xs), height: Math.max(...ys) - Math.min(...ys),
    left: Math.min(...xs), right: Math.max(...xs), top: Math.min(...ys), bottom: Math.max(...ys),
  };
}

const start = { clientX: 233, clientY: 284, pointerId: 1 };
canvas.onpointerdown(start);
const path = [
  [233, 274], [233, 284], [233, 294], [227, 304], [217, 312], [205, 318],
  [193, 319], [181, 314], [173, 304], [171, 292], [175, 280], [183, 269],
  [193, 260], [205, 254], [217, 251], [229, 252], [233, 254],
];
const bounds = [];
for (const [clientX, clientY] of path) {
  canvas.onpointermove({ clientX, clientY, pointerId: 1 });
  bounds.push(mouthBounds());
}

const hasVertical = bounds.some(b => b.height > b.width * 1.5);
const hasHorizontal = bounds.some(b => b.width > b.height * 1.5);
/* Ignore the first two release/threshold frames, which intentionally show the
 * compact kiss contour before the selected stretch family is fully engaged. */
if (!hasVertical || !hasHorizontal) {
  throw new Error(`stretch variant did not morph capsule into the wide blob: ${JSON.stringify(bounds)}`);
}
if (rotateCount !== 0) throw new Error(`live drag rotated the mouth contour ${rotateCount} times`);

canvas.onpointerup({ pointerId: 1 });
function dragBounds(dx, dy) {
  commands.length = 0;
  canvas.onpointerdown(start);
  canvas.onpointermove({ clientX: start.clientX + dx, clientY: start.clientY + dy, pointerId: 1 });
  const b = mouthBounds();
  canvas.onpointerup({ pointerId: 1 });
  return b;
}
const downStretch = dragBounds(0, 60);
const downStretchWithJitter = dragBounds(12, 60);
const horizontalStretch = dragBounds(60, 0);
const horizontalStretchWithJitter = dragBounds(60, 12);
const horizontalStretchLeft = dragBounds(-60, 0);
const upStretch = dragBounds(0, -60);
if (downStretch.width > downStretch.height * 0.7 ||
    downStretchWithJitter.width > downStretchWithJitter.height * 0.7 ||
    horizontalStretch.width <= horizontalStretch.height * 1.4 ||
    horizontalStretchWithJitter.width <= horizontalStretchWithJitter.height * 1.4 ||
    !(horizontalStretch.left > horizontalStretchLeft.left && horizontalStretch.right > horizontalStretchLeft.right) ||
    !(downStretch.top > upStretch.top && downStretch.bottom > upStretch.bottom)) {
  throw new Error(`stretch axis response is wrong: ${JSON.stringify({ downStretch, downStretchWithJitter, horizontalStretch, horizontalStretchWithJitter, horizontalStretchLeft, upStretch })}`);
}
/* Round and triangle families must have enough travel to match the Android
 * drag, and triangle must retain the actual drag vector instead of always
 * drawing the old upward-only contour.  Use diagonal vectors so the runtime
 * random-family selector is engaged rather than the horizontal stretch rule. */
randomValue = 0;
const roundSmall = dragBounds(16, 16);
const roundLarge = dragBounds(70, 70);
if (roundLarge.width < 100 || roundLarge.height < 90 ||
    roundLarge.width <= roundSmall.width + 35) {
  throw new Error(`round drag range is too small: ${JSON.stringify({ roundSmall, roundLarge })}`);
}
randomValue = 0.9;
const triangleShort = dragBounds(0, 30);
const triangleLong = dragBounds(0, 70);
if (Math.abs(triangleLong.width - triangleShort.width) > 1 ||
    triangleLong.height <= triangleShort.height + 6) {
  throw new Error(`triangle drag did not grow within its fixed screen orientation: ${JSON.stringify({ triangleShort, triangleLong })}`);
}
const triangleVertical = dragBounds(0, 60);
const triangleDiagonal = dragBounds(30, 60);
if (triangleDiagonal.width <= triangleVertical.width + 20 ||
    Math.abs(triangleVertical.height - triangleDiagonal.height) > 6) {
  throw new Error(`triangle contour rotated with the drag vector: ${JSON.stringify({ triangleVertical, triangleDiagonal })}`);
}
/* A live triangle is a three-edge contour.  Four quadratic edges used to
 * close the path through the tip twice, producing a rounded diamond instead
 * of one apex and one base. */
commands.length = 0;
canvas.onpointerdown(start);
canvas.onpointermove({ clientX: start.clientX, clientY: start.clientY + 60, pointerId: 1 });
const triangleFillIndex = commands.findLastIndex(c => c.type === 'fill' && c.points?.length);
const triangleFill = triangleFillIndex >= 0 ? commands[triangleFillIndex] : null;
const trianglePathStart = commands.findLastIndex((c, i) => i < triangleFillIndex && c.type === 'begin');
const triangleQuads = commands.slice(trianglePathStart, triangleFillIndex + 1).filter(c => c.type === 'quad');
canvas.onpointerup({ pointerId: 1 });
if (triangleQuads.length !== 2 || !triangleFill || triangleFill.points.length !== 6) {
  throw new Error(`triangle live contour is not a single apex/base path: ${JSON.stringify({ quadCount: triangleQuads.length, pointCount: triangleFill?.points?.length })}`);
}
randomValue = 0.6;
commands.length = 0;
canvas.onpointerdown(start);
canvas.onpointermove({ clientX: start.clientX + 36, clientY: start.clientY + 36, pointerId: 1 });
const squareBounds = mouthBounds();
canvas.onpointerup({ pointerId: 1 });
if (!(squareBounds.width > squareBounds.height && squareBounds.width / squareBounds.height < 1.3)) {
  throw new Error(`diagonal square variant was not independently selectable: ${JSON.stringify(squareBounds)}`);
}
commands.length = 0;
canvas.onpointerdown(start);
canvas.onpointermove({ clientX: start.clientX + 30, clientY: start.clientY + 45, pointerId: 1 });
const squareBoundsVertical = mouthBounds();
canvas.onpointerup({ pointerId: 1 });
if (Math.abs(squareBounds.width - squareBoundsVertical.width) < 4 && Math.abs(squareBounds.height - squareBoundsVertical.height) < 4) {
  throw new Error(`square variant ignored the pull vector: ${JSON.stringify({ squareBounds, squareBoundsVertical })}`);
}
randomValue = 0;
function sidePullBounds(dx) {
  commands.length = 0;
  translations.length = 0;
  scales.length = 0;
  const sideStart = { clientX: start.clientX + (dx > 0 ? 50 : -50), clientY: start.clientY, pointerId: 1 };
  canvas.onpointerdown(sideStart);
  canvas.onpointermove({ clientX: sideStart.clientX + dx, clientY: sideStart.clientY, pointerId: 1 });
  const b = mouthBounds();
  canvas.onpointerup({ pointerId: 1 });
  return {
    ...b,
    strokeCount: commands.filter(c => c.type === 'stroke').length,
    fillCount: commands.filter(c => c.type === 'fill').length,
    translateX: translations.at(-1)?.[0] ?? NaN,
    scaleX: scales.at(-1)?.[0] ?? 1,
  };
}
const sideRight = sidePullBounds(60);
const sideLeft = sidePullBounds(-60);
if (sideRight.strokeCount < 2 || sideLeft.strokeCount < 2 || sideRight.fillCount < 1 || sideLeft.fillCount < 1) {
  throw new Error(`left/right side pull did not render the captured 3+arc components: ${JSON.stringify({ sideRight, sideLeft })}`);
}
if (!(sideRight.translateX > 233 && sideLeft.translateX < 233 && sideRight.scaleX === 1 && sideLeft.scaleX === -1)) {
  throw new Error(`left/right side pull did not follow drag direction: ${JSON.stringify({ sideRight, sideLeft })}`);
}
const sideNear = sidePullBounds(20);
const sideFar = sidePullBounds(70);
if (sideFar.width > sideNear.width + 3 || sideFar.height > sideNear.height + 3) {
  throw new Error(`side pull mouth grows with distance instead of preserving its contour: ${JSON.stringify({ sideNear, sideFar })}`);
}

function renderResearch(label) {
  const b = buttons.find(candidate => candidate.textContent === label);
  if (!b || typeof b.onclick !== 'function') throw new Error(`research button missing: ${label}`);
  commands.length = 0;
  b.onclick();
  return mouthBounds();
}
const research = ['拉嘴·横向', '拉嘴·纵向', '拉嘴·斜向', '拉嘴·尖角'].map(renderResearch);
if (new Set(research.map(b => `${Math.round(b.width)}x${Math.round(b.height)}`)).size !== 4) {
  throw new Error(`research pull buttons collapsed to duplicate shapes: ${JSON.stringify(research)}`);
}
console.log(JSON.stringify({ path: 'down-semicircle', bounds, research }));
