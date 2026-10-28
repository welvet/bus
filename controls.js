let direction = 0;
let motorPending = false, servoPending = false, sending = false;
const element = id => document.querySelector('#' + id);
function stop() { direction = 0; motorPending = true; send(); }
function start(value) { direction = value; motorPending = true; send(); }
async function send() {
  if (sending) return;
  sending = true;
  while (motorPending || servoPending) {
    const motor = motorPending;
    if (motor) motorPending = false; else servoPending = false;
    const value = motor ? direction * Number(element('speed').value) : Number(element('angle').value);
    try {
      const response = await fetch(motor ? '/api/motor' : '/api/servo', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(value), keepalive: true, signal: AbortSignal.timeout(800)
      });
      if (!response.ok) throw Error('Command rejected');
      element('error').textContent = '';
    } catch (error) {
      element('error').textContent = 'Connection lost. Click a direction to restart.';
      const wasRunning = direction !== 0;
      direction = 0; servoPending = false; motorPending = wasRunning;
    }
  }
  sending = false;
}
element('forward').addEventListener('click', () => start(1));
element('reverse').addEventListener('click', () => start(-1));
element('stop').addEventListener('click', stop);
element('speed').addEventListener('input', () => {
  element('speed-value').textContent = element('speed').value + '%';
  if (direction) { motorPending = true; send(); }
});
element('angle').addEventListener('input', () => {
  element('angle-value').textContent = element('angle').value + '°';
  servoPending = true; send();
});
setInterval(() => { if (direction) { motorPending = true; send(); } }, 250);
function leave() { servoPending = false; stop(); }
window.addEventListener('pagehide', leave);
window.addEventListener('offline', leave);
document.addEventListener('visibilitychange', () => { if (document.hidden) leave(); });
async function info() {
  try {
    const response = await fetch('/api/info', {signal: AbortSignal.timeout(1000)});
    if (!response.ok) throw Error('Offline');
    const data = await response.json();
    element('status').textContent = 'Connected · ' + data.ip + ' · ' + data.rssi + ' dBm';
  } catch (error) {
    element('status').textContent = 'Disconnected';
    if (direction) leave();
  } finally { setTimeout(info, 1000); }
}
info();
