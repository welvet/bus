const vm = require('node:vm'), fs = require('node:fs'), assert = require('node:assert/strict');
class Element {
  constructor(value = '') { this.value = value; this.events = {}; }
  addEventListener(type, callback) { this.events[type] = callback; }
  emit(type) { this.events[type]?.(); }
}
(async () => {
  const ids = ['speed', 'speed-value', 'angle', 'angle-value', 'forward', 'reverse', 'stop', 'error', 'status'];
  const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
  elements.speed.value = '47'; elements.angle.value = '90';
  const document = new Element(), window = new Element(), timers = [], commands = [];
  document.querySelector = id => elements[id.slice(1)];
  let fail = false, delayServo = false, release;
  const context = {document, window, AbortSignal: {timeout: () => ({})}, setInterval: fn => timers.push(fn), setTimeout: () => {},
    fetch: async (url, options) => {
      if (!options) return {ok: true, json: async () => ({ip: '192.168.1.20', rssi: -45, speed: 0})};
      commands.push({url, value: JSON.parse(options.body)});
      if (fail) throw Error('Offline');
      if (delayServo && url === '/api/servo') { delayServo = false; await new Promise(resolve => release = resolve); }
      return {ok: true};
    }};
  vm.createContext(context); vm.runInContext(fs.readFileSync('controls.js', 'utf8'), context);
  const flush = () => new Promise(resolve => setImmediate(resolve));
  const last = () => commands.at(-1);
  await flush();
  elements.forward.emit('click'); await flush(); assert.deepEqual(last(), {url: '/api/motor', value: 47});
  for (let i = 0; i < 12; i++) { timers[0](); await flush(); assert.equal(last().value, 47); }
  elements.speed.value = '100'; elements.speed.emit('input'); await flush(); assert.equal(last().value, 100);
  elements.reverse.emit('click'); await flush(); assert.equal(last().value, -100);
  elements.stop.emit('click'); await flush(); assert.equal(last().value, 0);
  for (const type of ['offline', 'pagehide']) {
    elements.forward.emit('click'); await flush(); window.emit(type); await flush(); assert.equal(last().value, 0);
  }
  elements.forward.emit('click'); await flush(); document.hidden = true; document.emit('visibilitychange'); await flush(); assert.equal(last().value, 0);
  fail = true; elements.forward.emit('click'); await flush(); assert.equal(last().value, 0);
  const count = commands.length; fail = false; timers[0](); await flush(); assert.equal(commands.length, count);
  delayServo = true; elements.angle.value = '10'; elements.angle.emit('input'); await flush();
  elements.angle.value = '20'; elements.angle.emit('input'); elements.angle.value = '180'; elements.angle.emit('input');
  assert.deepEqual(last(), {url: '/api/servo', value: 10}); release(); await flush(); assert.equal(last().value, 180);
  elements.angle.value = '0'; elements.angle.emit('input'); await flush(); assert.equal(last().value, 0);
  console.log('UI direction, speed, servo coalescing, stop and connection-loss checks passed');
})().catch(error => {console.error(error); process.exitCode = 1;});
