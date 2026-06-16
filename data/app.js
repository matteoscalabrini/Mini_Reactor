const screens = {
  monitor: (root) => { root.innerHTML = '<div class="card">Monitor — wiring pending.</div>'; },
  run: (root) => { root.innerHTML = '<div class="card">Run — wiring pending.</div>'; },
  settings: (root) => { root.innerHTML = '<div class="card">Settings — wiring pending.</div>'; },
};
let teardown = null;
function route() {
  const name = (location.hash.replace('#', '') || 'monitor');
  const tab = screens[name] ? name : 'monitor';
  document.querySelectorAll('.tab').forEach((a) => a.classList.toggle('on', a.dataset.tab === tab));
  const root = document.getElementById('screen');
  if (typeof teardown === 'function') teardown();
  root.innerHTML = '';
  teardown = screens[tab](root) || null;
}
window.addEventListener('hashchange', route);
route();
