// Keep renewing the selected direction until Stop or connection/page loss.
let direction = [0, 0];
let timer = null;
let sending = false;
let pending = false;

function stop() {
  direction = [0, 0];
  clearInterval(timer);
  timer = null;
  sendCommand();
}

async function sendCommand() {
  pending = true;
  if (sending) return;
  sending = true;
  while (pending) {
    pending = false;
    const speed = Math.round(Number(document.querySelector('#speed').value) * 255 / 100);
    const [m1, m2] = direction.map(value => value * speed);
    try {
      const response = await fetch('/motors', {
        method: 'POST', headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({m1, m2}), keepalive: true,
        signal: AbortSignal.timeout(500)
      });
      if (!response.ok) throw Error('Motor command failed');
      document.querySelector('#error').textContent = '';
    } catch (error) {
      document.querySelector('#error').textContent = 'Connection lost. Click a direction to restart.';
      if (timer !== null) {
        direction = [0, 0];
        clearInterval(timer);
        timer = null;
        pending = true;
      }
    }
  }
  sending = false;
}

function start(button) {
  direction = [Number(button.dataset.m1), Number(button.dataset.m2)];
  if (timer === null) timer = setInterval(sendCommand, 100);
  sendCommand();
}

function connectionLost() {
  if (timer !== null) stop();
}

for (const button of document.querySelectorAll('[data-m1]')) {
  button.addEventListener('click', () => start(button));
}
document.querySelector('#stop').addEventListener('click', stop);
window.addEventListener('pagehide', stop);
window.addEventListener('offline', stop);
document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
