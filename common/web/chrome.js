/* miniwave shared chrome — header + footer injected into pages that opt in */
(function() {
'use strict';

const PAGE = location.pathname;
const PAGES = [
  { href: '/', label: 'rack', color: 'var(--accent, #e94560)' },
  { href: '/keyseq', label: 'keyseq', color: '#8866cc' },
  { href: '/effex', label: 'effex', color: '#00d4aa' },
  { href: '/chords', label: 'chords', color: '#ff8800' },
  { href: '/config', label: 'cfg', color: '#666' },
];

window.mwNav = function() {
  return PAGES.map(p => {
    const active = p.href === PAGE;
    if (active) return `<span style="color:${p.color};font-weight:bold">${p.label}</span>`;
    return `<a href="${p.href}" style="color:${p.color};text-decoration:none">${p.label}</a>`;
  }).join(' / ');
};

/* Only inject chrome if page doesn't have its own topbar */
if (!document.getElementById('topbar') && !document.getElementById('mw-topbar')) {
  const hdr = document.createElement('div');
  hdr.id = 'mw-topbar';
  hdr.innerHTML = `
    <div style="font-size:18px;font-weight:bold;letter-spacing:2px">${mwNav()}</div>
    <div style="display:flex;align-items:center;gap:6px">
      <input type="text" id="mw-bpm" value="120" style="width:40px;background:transparent;border:none;border-bottom:1px solid var(--border,#2a2a4a);color:var(--text,#e0e0e0);font-family:inherit;font-size:12px;text-align:center;padding:2px">
      <span style="color:var(--dim,#666);font-size:10px;text-transform:uppercase">bpm</span>
      <button id="mw-tap" style="padding:3px 8px;font-size:10px;font-weight:bold;border-radius:4px;background:var(--card,#16213e);border:none;color:var(--text,#e0e0e0);cursor:pointer">TAP</button>
    </div>
    <div style="display:flex;align-items:center;gap:8px">
      <span id="mw-midi-badge" style="color:var(--dim,#666);font-size:10px"></span>
      <span id="mw-version" style="color:var(--dim,#666);font-size:10px"></span>
    </div>
  `;
  Object.assign(hdr.style, {
    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
    padding: '8px 16px', background: 'var(--panel, #111)',
    borderBottom: '1px solid var(--border, #2a2a4a)', height: '48px', flexShrink: '0',
    fontFamily: "'SF Mono','Fira Code','Cascadia Code',monospace", fontSize: '13px',
    color: 'var(--text, #eee)'
  });
  document.body.prepend(hdr);

  const ftr = document.createElement('div');
  ftr.id = 'mw-footer';
  ftr.innerHTML = `
    <span id="mw-f-audio">audio: ---</span>
    <span id="mw-f-midi">midi: ---</span>
    <span id="mw-f-sse">sse: ---</span>
    <span id="mw-f-ver" style="margin-left:auto">...</span>
  `;
  Object.assign(ftr.style, {
    display: 'flex', alignItems: 'center', gap: '16px',
    padding: '4px 16px', background: 'var(--panel, #111)',
    borderTop: '1px solid var(--border, #2a2a4a)', height: '28px', flexShrink: '0',
    fontFamily: "'SF Mono','Fira Code','Cascadia Code',monospace",
    fontSize: '10px', color: 'var(--dim, #aaa)'
  });
  document.body.append(ftr);
}

/* ── SSE for shared state ── */
const es = window._mwSSE || new EventSource('/events');
window._mwSSE = es;

es.addEventListener('rack_status', e => {
  const d = JSON.parse(e.data);
  const v = document.getElementById('mw-version');
  const fv = document.getElementById('mw-f-ver');
  if (v) v.textContent = 'v' + d.version;
  if (fv) fv.textContent = 'v' + d.version;
  const bpm = document.getElementById('mw-bpm');
  if (bpm && document.activeElement !== bpm) bpm.value = d.bpm;
  const mb = document.getElementById('mw-midi-badge');
  if (mb) mb.textContent = d.midi_device || 'no midi';
  const fa = document.getElementById('mw-f-audio');
  if (fa) fa.textContent = 'audio: ' + d.audio_backend + ' ' + d.sample_rate + 'Hz';
  const fm = document.getElementById('mw-f-midi');
  if (fm) fm.textContent = 'midi: ' + (d.midi_device || 'none');
  const fs = document.getElementById('mw-f-sse');
  if (fs) fs.textContent = 'sse: ' + d.sse_clients;
});

/* BPM + tap */
const bpmInput = document.getElementById('mw-bpm');
const tapBtn = document.getElementById('mw-tap');
let tapTimes = [];

if (bpmInput) bpmInput.addEventListener('change', () => {
  const v = parseFloat(bpmInput.value);
  if (v > 0 && v <= 999)
    fetch('/api', {method:'POST', body:JSON.stringify({type:'bpm', value:v})});
});

if (tapBtn) tapBtn.addEventListener('click', () => {
  const now = performance.now();
  tapTimes.push(now);
  if (tapTimes.length > 8) tapTimes.shift();
  if (tapTimes.length >= 2) {
    const ivs = [];
    for (let i = 1; i < tapTimes.length; i++) ivs.push(tapTimes[i] - tapTimes[i-1]);
    const avg = ivs.reduce((a,b) => a+b) / ivs.length;
    const bpm = Math.round(60000 / avg);
    if (bpm > 20 && bpm < 400) {
      bpmInput.value = bpm;
      fetch('/api', {method:'POST', body:JSON.stringify({type:'bpm', value:bpm})});
    }
  }
  clearTimeout(tapBtn._r);
  tapBtn._r = setTimeout(() => { tapTimes = []; }, 2000);
});

/* SW */
if ('serviceWorker' in navigator) navigator.serviceWorker.register('/sw.js');
})();
