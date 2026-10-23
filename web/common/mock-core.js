'use strict';

/* =========================================================================
 * mock-core.js -- a fake device living entirely in JavaScript, for
 * exercising a whole panel with no hardware (?mock=1). Produces real wire
 * bytes into the same parser used for a real device -- this is not a
 * UI-only stub. See docs/17-web-control-panel.md.
 *
 * This file knows the 15 sensor-agnostic verbs and the generic SENS/
 * SCAPS/SCFG/CAL shape. It knows nothing about any one sensor module --
 * that comes from the `profile` object each page passes to the
 * constructor:
 *
 *   new MockSerialPort({
 *     sensRow:  [name, type, bus, addr, state],   // one SENS row
 *     channels: [[name, type, access], ...],      // for CAPS
 *     cfgDefs:  [{name, type, min, max, def, unit, enumValues}, ...],
 *     channelValue(name, cfgValues) { ... },       // live value for GET/streaming
 *     action: null, or (actionName, arg) => result or null if unknown,
 *     fault: new URLSearchParams(location.search).get('fault'),
 *   })
 *
 * Add &fault=err|crc|drop to the page's URL to inject a failure mode.
 * ========================================================================= */

class MockSerialPort {
  constructor(profile) {
    this.profile = profile;
    this._fault = profile.fault || null;
    this._toDevice = [];
    this._buildState();
    this.readable = {
      getReader: () => ({
        read: () => this._readPromise(),
        cancel: async () => { this._cancelled = true; },
      }),
    };
    this.writable = {
      getWriter: () => ({
        write: async (bytes) => this._handleIncoming(bytes),
        close: async () => {},
        releaseLock: () => {},
      }),
    };
    this._queue = [];
    this._queueResolve = null;
    this._startSim();
  }

  async open() {}
  async close() { this._stopped = true; }
  getInfo() { return { usbVendorId: USB_VID, usbProductId: USB_PID }; }

  _readPromise() {
    if (this._cancelled) return Promise.resolve({ value: undefined, done: true });
    if (this._queue.length) return Promise.resolve({ value: this._queue.shift(), done: false });
    return new Promise((resolve) => {
      this._queueResolve = resolve;
      this._waitTimer = setTimeout(() => {
        if (this._queueResolve) {
          this._queueResolve({ value: new Uint8Array(0), done: false });
          this._queueResolve = null;
        }
      }, 50);
    });
  }

  _pushOut(bytes) {
    if (this._queueResolve) {
      clearTimeout(this._waitTimer);
      this._queueResolve({ value: bytes, done: false });
      this._queueResolve = null;
    } else {
      this._queue.push(bytes);
    }
  }

  _sendFrame(items) {
    if (this._fault === 'drop' && Math.random() < 0.1) return; // silently drop a response
    const payload = CBOR.encode(items);
    let crc = crc16(payload);
    if (this._fault === 'crc' && Math.random() < 0.1) crc ^= 0xFFFF; // corrupt it
    const framed = new Uint8Array([...payload, (crc >> 8) & 0xFF, crc & 0xFF]);
    const encoded = cobsEncode(framed);
    this._pushOut(new Uint8Array([...encoded, 0x00]));
  }

  _handleIncoming(bytes) {
    this._rxAccum = this._rxAccum ? new Uint8Array([...this._rxAccum, ...bytes]) : new Uint8Array(bytes);
    const idx = this._rxAccum.indexOf(0);
    if (idx < 0) return;
    const wire = this._rxAccum.slice(0, idx);
    this._rxAccum = this._rxAccum.slice(idx + 1);
    let framed;
    try { framed = cobsDecode(wire); } catch (e) { return; }
    const payload = framed.slice(0, framed.length - 2);
    let cmd;
    try { cmd = CBOR.decode(payload); } catch (e) { return; }
    this._handleCommand(cmd);
  }

  _buildState() {
    this.cfg = { PERIOD_US: 100000, BATCH: 1, MODE: 'PERIODIC', ENCODING: 'CBOR', LED_MODE: 'STATUS' };
    this.subs = new Set();
    this.streaming = false;
    this.led = [0, 0, 0];
    this.cfgValues = {};
    // Store a bool key's value as a genuine JS boolean, matching the real
    // device's wire encoding for SENSOR_VAL_BOOL (sensor_core.c encodes
    // and expects a CBOR boolean, not 0/1) -- see
    // components/sensor_core/src/sensor_core.c, reply_cfg_value() and
    // parse_cfg_value().
    for (const d of this.profile.cfgDefs) this.cfgValues[d.name] = (d.type === 'bool') ? !!d.def : d.def;
  }

  _startSim() {
    this._simTimer = setInterval(() => {
      if (this._stopped || !this.streaming) return;
      for (const ch of this.subs) {
        this._sendFrame([FRAME_KIND.DATA, this._seq(), Math.floor(performance.now() * 1000), ch,
          this.profile.channelValue(ch, this.cfgValues), 0]);
      }
    }, 100);
  }
  _seq() {
    this._s = ((this._s || 0) % 65535) + 1;
    return this._s;
  }

  _handleCommand(cmd) {
    const [, seq, verb, ...args] = cmd;
    const ok = (...values) => this._sendFrame([FRAME_KIND.OK, seq, verb, ...values]);
    const row = (...cols) => this._sendFrame([FRAME_KIND.ROW, seq, verb, ...cols]);
    const err = (code) => this._sendFrame([FRAME_KIND.ERR, seq, verb, code]);

    if (this._fault === 'err' && Math.random() < 0.1 && verb !== 'STOP') { err('EINTERNAL'); return; }

    switch (verb) {
      case 'PING': return ok();
      case 'ID': return ok('ESP32-S3 Sensor Device (mock)', '0.1', 'MOCK0001');
      case 'CAPS': {
        const chs = [['led0', 'bytes', 'RW'], ['uptime_us', 'uint', 'RO'], ...this.profile.channels];
        chs.forEach((c) => row(...c));
        return ok(chs.length);
      }
      case 'STAT': return ok(Math.floor(performance.now() * 1000), 10, 10, 0, 0, 0);
      case 'GET': {
        if (args[0] === '*') {
          const chs = ['led0', 'uptime_us', ...this.profile.channels.map((c) => c[0])];
          chs.forEach((name) => row(name, this._channelValue(name)));
          return ok(chs.length);
        }
        return ok(this._channelValue(args[0]));
      }
      case 'SET': {
        if (args[0] === 'led0') { this.led = Array.from(args[1]); this.cfg.LED_MODE = 'MANUAL'; return ok(); }
        return err('EACCESS');
      }
      case 'CFG': {
        const key = args[0];
        if (!(key in this.cfg)) return err('EARG');
        if (args.length > 1) this.cfg[key] = args[1];
        return ok(this.cfg[key]);
      }
      case 'SUB': this.subs.add(args[0]); return ok();
      case 'UNSUB': this.subs.delete(args[0]); return ok();
      case 'START': this.streaming = true; return ok();
      case 'STOP': this.streaming = false; return ok();
      case 'SAVE': return ok();
      case 'LOAD': return ok();
      case 'SENS': row(...this.profile.sensRow); return ok(1);
      case 'SCAPS': {
        for (const d of this.profile.cfgDefs) {
          const def = d.type === 'enum' ? d.enumValues[d.def] : d.type === 'bool' ? !!d.def : d.def;
          row(d.name, d.type, d.min, d.max, def, d.unit, d.enumValues ? d.enumValues.join(',') : null);
        }
        return ok(this.profile.cfgDefs.length);
      }
      case 'SCFG': {
        const def = this.profile.cfgDefs.find((d) => d.name === args[0]);
        if (!def) return err('ENOCH');
        if (args.length > 1) {
          let v = args[1];
          if (def.type === 'enum') {
            const idx = def.enumValues.indexOf(v);
            if (idx < 0) return err('ERANGE');
            v = idx;
          } else if (v < def.min || v > def.max) {
            return err('ERANGE');
          }
          this.cfgValues[def.name] = v;
        }
        const raw = this.cfgValues[def.name];
        return ok(def.type === 'enum' ? def.enumValues[raw] : raw);
      }
      case 'CAL': {
        if (!this.profile.action) return err('ENOCH');
        if (this.streaming) return err('ESTATE');
        const result = this.profile.action(args[0], args[1]);
        if (result === null || result === undefined) return err('ENOCH');
        setTimeout(() => ok(result), 400);
        return;
      }
      default: return err('ECMD');
    }
  }

  _channelValue(name) {
    if (name === 'led0') return new Uint8Array(this.led);
    if (name === 'uptime_us') return Math.floor(performance.now() * 1000);
    return this.profile.channelValue(name, this.cfgValues);
  }
}
