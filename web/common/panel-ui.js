'use strict';

/* =========================================================================
 * panel-ui.js -- everything a sensor panel needs that is not specific to
 * one sensor module: preferences, the activity log, the status pill, the
 * connect/disconnect sequence, the SCAPS-built configuration card, the
 * LED card, and the animation loop. See docs/17-web-control-panel.md.
 *
 * A page calls Panel.start(opts) once, supplying only what is specific to
 * its own sensor module -- see that function's doc comment below.
 * Requires protocol.js (and, under ?mock=1, mock-core.js) loaded first.
 * ========================================================================= */

const Panel = (() => {

/* =========================================================================
 * Preferences -- one localStorage record per page, keyed by opts.prefsKey
 * so the two panels do not collide.
 * ========================================================================= */

let prefs = {};
function loadPrefs(key) {
  try { return JSON.parse(localStorage.getItem('esp32s3-panel-' + key)) || {}; } catch (e) { return {}; }
}
function savePrefs(key, p) {
  try { localStorage.setItem('esp32s3-panel-' + key, JSON.stringify(p)); } catch (e) {}
}

/* =========================================================================
 * Shared app state. A page keeps its own sensor-specific state (sample
 * arrays, chart data) in its own closure; this is only what every panel
 * needs regardless of sensor.
 * ========================================================================= */

const state = {
  link: null,
  connected: false,
  sensInstances: [],     // from SENS
  channels: [],           // from CAPS
  cfgKeys: [],             // from SCAPS
  cfgValues: new Map(),    // key -> last known value
  statsPollTimer: null,
};

/* =========================================================================
 * Activity log, status pill, banner
 * ========================================================================= */

function logLine(level, text) {
  const verboseEl = document.getElementById('verboseCheck');
  const verbose = verboseEl ? verboseEl.checked : false;
  if (level === 'info' && !verbose) return;
  const div = document.createElement('div');
  div.className = 'line ' + (level === 'warn' ? 'warn' : level === 'err' ? 'err' : '');
  div.textContent = new Date().toLocaleTimeString() + '  ' + text;
  const log = document.getElementById('log');
  if (!log) return;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

function setStatusPill(cls, text) {
  const pill = document.getElementById('statusPill');
  if (!pill) return;
  pill.className = 'pill' + (cls ? ' ' + cls : '');
  document.getElementById('statusText').textContent = text;
}

function showBanner(text) {
  const b = document.getElementById('topBanner');
  if (!b) return;
  b.textContent = text;
  b.hidden = false;
}

function mockRequested() {
  return new URLSearchParams(location.search).get('mock') === '1';
}

function fmt(v) { return v === null || v === undefined ? '&mdash;' : v; }
function fmtUptime(us) {
  const s = Math.floor(us / 1e6);
  return Math.floor(s / 60) + 'm' + (s % 60) + 's';
}

/* =========================================================================
 * Canvas sizing helper, for any chart a page draws.
 * ========================================================================= */

function fitCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const w = Math.max(1, Math.round(rect.width * dpr));
  const h = Math.max(1, Math.round(rect.height * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  return { ctx: canvas.getContext('2d'), w, h, dpr };
}

/* =========================================================================
 * Configuration card -- built entirely from SCAPS. No sensor-specific
 * code here: a different sensor module, with a different key list,
 * renders correctly with no change to this function. See
 * docs/17-web-control-panel.md.
 * ========================================================================= */

function buildCfgTable() {
  const tbody = document.querySelector('#cfgTable tbody');
  if (!tbody) return;
  tbody.innerHTML = '';
  for (const key of state.cfgKeys) {
    const tr = document.createElement('tr');
    const tdKey = document.createElement('td');
    tdKey.className = 'key';
    tdKey.textContent = key.name;
    tr.appendChild(tdKey);

    const tdCtl = document.createElement('td');
    let input;
    if (key.type === 'enum') {
      input = document.createElement('select');
      for (const v of key.enumValues) {
        const opt = document.createElement('option');
        opt.value = v; opt.textContent = v;
        input.appendChild(opt);
      }
    } else if (key.type === 'bool') {
      input = document.createElement('select');
      for (const v of ['false', 'true']) {
        const opt = document.createElement('option');
        opt.value = v; opt.textContent = v;
        input.appendChild(opt);
      }
    } else {
      input = document.createElement('input');
      input.type = 'number';
      input.min = key.min; input.max = key.max;
    }
    input.dataset.key = key.name;
    tdCtl.appendChild(input);
    tr.appendChild(tdCtl);

    const tdUnit = document.createElement('td');
    tdUnit.className = 'unit';
    tdUnit.textContent = key.unit || '';
    tr.appendChild(tdUnit);

    const tdBtn = document.createElement('td');
    const btn = document.createElement('button');
    btn.className = 'small';
    btn.textContent = 'Set';
    tdBtn.appendChild(btn);
    const status = document.createElement('span');
    status.style.marginLeft = '8px';
    tdBtn.appendChild(status);
    tr.appendChild(tdBtn);

    btn.addEventListener('click', () => writeCfgKey(key, input, status));
    tbody.appendChild(tr);
  }
}

function parseCfgInput(key, input) {
  if (key.type === 'bool') return input.value === 'true';
  if (key.type === 'enum') return input.value;
  return Number(input.value);
}

function setInputValue(input, key, value) {
  if (key.type === 'bool') input.value = value ? 'true' : 'false';
  else input.value = value;
}

async function writeCfgKey(key, input, status) {
  status.textContent = '...';
  status.className = '';
  try {
    const r = await state.link.send('SCFG', key.name, parseCfgInput(key, input));
    const value = r.values[0];
    state.cfgValues.set(key.name, value);
    status.textContent = 'verified: ' + value;
    status.className = 'verify-ok';
    setInputValue(input, key, value);
  } catch (e) {
    status.textContent = e instanceof ProtocolError ? e.code : 'error';
    status.className = 'verify-bad';
    logLine('warn', key.name + ': ' + e.message);
  }
}

async function refreshCfgValues() {
  for (const key of state.cfgKeys) {
    try {
      const r = await state.link.send('SCFG', key.name);
      state.cfgValues.set(key.name, r.values[0]);
      const input = document.querySelector('[data-key="' + key.name + '"]');
      if (input) setInputValue(input, key, r.values[0]);
    } catch (e) {}
  }
}

/* =========================================================================
 * LED card -- generic; the same led0 channel on every device assembly.
 * ========================================================================= */

const LED_SWATCHES = [
  [0, 0, 0], [255, 255, 255], [255, 0, 0], [255, 140, 0],
  [255, 220, 0], [0, 200, 0], [0, 120, 255], [160, 0, 220],
];
function buildLedSwatches() {
  const row = document.getElementById('ledSwatches');
  if (!row) return;
  row.innerHTML = '';
  for (const [r, g, b] of LED_SWATCHES) {
    const sw = document.createElement('div');
    sw.className = 'swatch';
    sw.style.background = 'rgb(' + r + ',' + g + ',' + b + ')';
    sw.addEventListener('click', () => setLed(r, g, b));
    row.appendChild(sw);
  }
}
async function setLed(r, g, b) {
  try {
    await state.link.send('SET', 'led0', new Uint8Array([r, g, b]));
    const preview = document.getElementById('ledPreview');
    if (preview) preview.style.background = 'rgb(' + r + ',' + g + ',' + b + ')';
    refreshLedMode();
  } catch (e) { logLine('warn', 'SET led0: ' + e.message); }
}
async function refreshLedMode() {
  try {
    const r = await state.link.send('CFG', 'LED_MODE');
    const el = document.getElementById('ledModeText');
    if (el) el.textContent = 'LED_MODE: ' + r.values[0];
  } catch (e) {}
}

/* =========================================================================
 * Health polling -- STAT's generic link counters, every 2 seconds. A
 * page's own sensor health card is driven by opts.onPoll(map, statValues),
 * from the same cycle's GET * rows.
 * ========================================================================= */

async function pollHealth(opts) {
  if (!state.connected) return;
  let map = {};
  try {
    const r = await state.link.send('GET', '*');
    for (const row of r.rows) map[row[0]] = row[1];
  } catch (e) { /* a transient failure here is not worth surfacing */ }
  let statValues = null;
  try {
    const r = await state.link.send('STAT');
    statValues = r.values;
    const [uptimeUs, rx, tx, crcErr, dropped] = r.values;
    const el = document.getElementById('linkStats');
    if (el) {
      el.innerHTML =
        '<span>uptime: <b>' + fmtUptime(uptimeUs) + '</b></span>' +
        '<span>frames rx/tx: <b>' + rx + ' / ' + tx + '</b></span>' +
        '<span>crc errors: <b>' + crcErr + '</b></span>' +
        '<span>dropped: <b>' + dropped + '</b></span>';
    }
  } catch (e) {}
  if (opts.onPoll) opts.onPoll(map, statValues);
}

/* =========================================================================
 * Connection flow
 * ========================================================================= */

async function probeGrantedPorts() {
  if (!('serial' in navigator)) return null;
  let candidates;
  try { candidates = await navigator.serial.getPorts(); } catch (e) { return null; }
  for (const candidate of candidates) {
    const info = candidate.getInfo ? candidate.getInfo() : {};
    if (info.usbVendorId !== USB_VID || info.usbProductId !== USB_PID) continue;
    let opened = false, probe = null;
    try {
      await candidate.open({ baudRate: 115200, bufferSize: 4096 });
      opened = true;
      probe = new Link(candidate);
      probe._attach();
      await probe.send('PING');
      return probe;
    } catch (e) {
      if (probe) { try { await probe.close(); } catch (_e) {} }
      else if (opened) { try { await candidate.close(); } catch (_e) {} }
    }
  }
  return null;
}

async function connect(opts) {
  setStatusPill('', 'Connecting...');
  const connectBtn = document.getElementById('connectBtn');
  connectBtn.disabled = true;
  try {
    let link = await probeGrantedPorts();
    if (!link) {
      let port;
      if (mockRequested()) {
        port = new MockSerialPort(Object.assign({
          fault: new URLSearchParams(location.search).get('fault'),
        }, opts.mockProfile));
      } else {
        const allPorts = document.getElementById('allPortsCheck');
        const opts2 = allPorts && allPorts.checked
          ? {} : { filters: [{ usbVendorId: USB_VID, usbProductId: USB_PID }] };
        port = await navigator.serial.requestPort(opts2);
      }
      link = new Link(port);
      await link.open();
      await link.send('PING');
    }
    link.addEventListener('sample', (e) => { if (opts.onSample) opts.onSample(e.detail); });
    link.addEventListener('framingerror', (e) => logLine('warn', 'framing error: ' + e.detail.reason));

    state.link = link;
    state.connected = true;

    const id = await link.send('ID');
    const deviceName = document.getElementById('deviceName');
    if (deviceName) deviceName.textContent = id.values[0];
    const deviceMeta = document.getElementById('deviceMeta');
    if (deviceMeta) deviceMeta.textContent = 'fw ' + id.values[1] + ' · serial ' + id.values[2];
    const deviceId = document.getElementById('deviceId');
    if (deviceId) deviceId.hidden = false;

    try { await link.send('STOP'); } catch (e) { /* ESTATE if already stopped */ }

    const sens = await link.send('SENS');
    state.sensInstances = sens.rows.map((r) => ({ name: r[0], type: r[1], bus: r[2], addr: r[3], state: r[4] }));
    const sensMeta = document.getElementById('sensMeta');
    if (sensMeta) {
      sensMeta.textContent = state.sensInstances
        .map((i) => i.name + ' (' + i.type + ', ' + i.state + ')').join(', ');
    }

    const caps = await link.send('CAPS');
    state.channels = caps.rows.map((r) => ({ name: r[0], type: r[1], access: r[2] }));

    const scaps = await link.send('SCAPS');
    state.cfgKeys = scaps.rows.map((r) => ({
      name: r[0], type: r[1], min: r[2], max: r[3], defaultValue: r[4], unit: r[5],
      enumValues: r[6] ? r[6].split(',') : null,
    }));
    buildCfgTable();
    await refreshCfgValues();

    for (const ch of opts.subscribe || []) {
      try { await link.send('SUB', ch); } catch (e) {}
    }
    await link.send('START');

    try {
      const ledR = await link.send('GET', 'led0');
      const rgb = ledR.values[0];
      const preview = document.getElementById('ledPreview');
      if (preview) preview.style.background = 'rgb(' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ')';
    } catch (e) {}
    refreshLedMode();

    setStatusPill('ok', 'Connected');
    connectBtn.hidden = true;
    document.getElementById('disconnectBtn').hidden = false;
    logLine('info', 'connected');

    state.statsPollTimer = setInterval(() => pollHealth(opts), 2000);
    pollHealth(opts);
  } catch (e) {
    setStatusPill('bad', 'Connect failed');
    logLine('err', e.message);
  } finally {
    connectBtn.disabled = false;
  }
}

async function disconnect() {
  if (state.statsPollTimer) clearInterval(state.statsPollTimer);
  if (state.link) {
    try { await state.link.send('STOP'); } catch (e) {}
    try { await state.link.close(); } catch (e) {}
  }
  state.connected = false;
  state.link = null;
  setStatusPill('', 'Not connected');
  document.getElementById('connectBtn').hidden = false;
  document.getElementById('disconnectBtn').hidden = true;
  logLine('info', 'disconnected');
}

/* =========================================================================
 * Animation loop -- calls the page's own draw() once per frame while
 * connected.
 * ========================================================================= */

function startAnimation(opts) {
  function animate() {
    if (state.connected && opts.draw) opts.draw();
    requestAnimationFrame(animate);
  }
  requestAnimationFrame(animate);
}

/* =========================================================================
 * Boot: self-test gate, prefs, wiring, mock-mode log line.
 *
 * opts:
 *   prefsKey    short id for this page's localStorage record, e.g. "ldc1612"
 *   subscribe   channel names to SUB at connect time
 *   onSample(detail)        routes one DATA frame
 *   onPoll(map, statValues) called every 2s with GET * rows and STAT
 *   draw()                  called once per animation frame while connected
 *   mockProfile              see mock-core.js
 * ========================================================================= */

function start(opts) {
  prefs = loadPrefs(opts.prefsKey || 'panel');

  if (window.isSecureContext === false) {
    showBanner('Web Serial needs a secure context (file:// or https://, not plain http://).');
  }
  if (!('serial' in navigator) && !mockRequested()) {
    showBanner('This browser has no Web Serial API. Use Chrome or Edge, or add ?mock=1 to try the page without hardware.');
    document.getElementById('connectBtn').disabled = true;
  }

  const results = runSelfTest();
  const failed = results.filter((r) => !r.pass);
  window.__selfTest = () => results;
  if (failed.length) {
    showBanner('Internal self-test failed (' + failed.length + ' check' +
      (failed.length > 1 ? 's' : '') + ') — Connect is disabled. See the console for details.');
    console.error('Self-test failures:', failed);
    document.getElementById('connectBtn').disabled = true;
  } else {
    console.log('Self-test: all ' + results.length + ' checks passed.');
  }

  buildLedSwatches();

  document.getElementById('connectBtn').addEventListener('click', () => connect(opts));
  document.getElementById('disconnectBtn').addEventListener('click', disconnect);
  document.getElementById('ledStatusModeBtn').addEventListener('click', async () => {
    try { await state.link.send('CFG', 'LED_MODE', 'STATUS'); refreshLedMode(); } catch (e) {}
  });
  window.addEventListener('pagehide', () => {
    if (state.link) state.link.send('STOP').catch(() => {});
  });

  startAnimation(opts);

  if (mockRequested()) {
    logLine('info', 'mock mode: ?mock=1' + (new URLSearchParams(location.search).get('fault') ? ' fault=' + new URLSearchParams(location.search).get('fault') : ''));
  }
}

return { start, state, logLine, fitCanvas, fmt, fmtUptime, mockRequested };

})();
