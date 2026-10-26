// Refresh the command only while a direction button is held.
let held = null;
let direction = [0, 0];
let timer = null;
let sending = false;
let pending = false;

function stop() {
  held = null;
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
      document.querySelector('#error').textContent = 'Connection lost. Release and press again.';
      if (held !== null) {
        held = null;
        direction = [0, 0];
        clearInterval(timer);
        timer = null;
        pending = true;
      }
    }
  }
  sending = false;
}

function start(button, id) {
  if (held !== null) return;
  held = id;
  direction = [Number(button.dataset.m1), Number(button.dataset.m2)];
  sendCommand();
  timer = setInterval(sendCommand, 100);
}

for (const button of document.querySelectorAll('[data-m1]')) {
  button.addEventListener('pointerdown', event => {
    if (event.button !== 0 || held !== null) return;
    event.preventDefault();
    button.setPointerCapture(event.pointerId);
    start(button, event.pointerId);
  });
  for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
    button.addEventListener(type, event => {
      if (held === event.pointerId) stop();
    });
  }
  button.addEventListener('keydown', event => {
    if (event.key === ' ' || event.key === 'Enter') {
      event.preventDefault();
      if (!event.repeat) start(button, event.key);
    }
  });
  button.addEventListener('keyup', event => {
    if (held === event.key) { event.preventDefault(); stop(); }
  });
  button.addEventListener('blur', () => { if (held !== null) stop(); });
  button.addEventListener('contextmenu', event => event.preventDefault());
}
document.querySelector('#stop').addEventListener('click', stop);
window.addEventListener('blur', stop);
window.addEventListener('pagehide', stop);
document.addEventListener('visibilitychange', () => { if (document.hidden) stop(); });
