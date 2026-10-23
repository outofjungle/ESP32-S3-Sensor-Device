'use strict';

/* =========================================================================
 * protocol.js -- wire codecs and transport, shared by every sensor panel.
 * Nothing here names a sensor module. See docs/17-web-control-panel.md.
 *
 * Loaded as a classic <script>, not a module: every top-level const/class
 * here is visible to panel.js, mock-core.js, and the page's own inline
 * script, in that load order -- see docs/17-web-control-panel.md on why
 * (Chrome blocks module and fetch() loads from file://, but not a plain
 * <script src> tag).
 * ========================================================================= */

/* =========================================================================
 * S1: Constants
 * ========================================================================= */
const FRAME_KIND = { CMD: 0, OK: 1, ERR: 2, ROW: 3, DATA: 4 };
const USB_VID = 0x303A; // Espressif
const USB_PID = 0x8002;
const CMD_TIMEOUT_MS = 1000;
const CAL_TIMEOUT_MS = 15000;
const MAX_WIRE_FRAME = 512;

const ERROR_MEANING = {
  EFRAME: 'the frame did not match the expected structure',
  ECMD: 'the verb is not a known command',
  EARG: 'an argument is missing or invalid',
  ERANGE: 'a value is outside the range this channel or key accepts',
  ESTATE: "the command does not apply to the device's current state",
  EVERIFY: 'the device wrote a value, but read back something different',
  ENOCH: 'the channel, key, or instance name is not known',
  EACCESS: "the channel's access mode does not allow this",
  EINTERNAL: 'the device found an internal fault',
};

/* =========================================================================
 * S2: Wire codecs -- COBS, CRC-16/CCITT-FALSE, and a small CBOR subset.
 * See docs/06-cbor-encoding.md. Ported from the same algorithms used in
 * components/device_proto (main/cobs.c, main/crc16.c).
 * ========================================================================= */

function cobsEncode(data) {
  const out = [0]; // placeholder for first code byte
  let codePos = 0;
  let code = 1;
  for (let i = 0; i < data.length; i++) {
    const b = data[i];
    if (b === 0) {
      out[codePos] = code;
      codePos = out.length;
      out.push(0);
      code = 1;
    } else {
      out.push(b);
      code++;
      if (code === 0xFF) {
        out[codePos] = code;
        codePos = out.length;
        out.push(0);
        code = 1;
      }
    }
  }
  out[codePos] = code;
  return new Uint8Array(out);
}

function cobsDecode(data) {
  const out = [];
  let idx = 0;
  while (idx < data.length) {
    const code = data[idx];
    if (code === 0) throw new Error('cobs: zero code byte');
    const chunkLen = code - 1;
    if (idx + 1 + chunkLen > data.length) throw new Error('cobs: truncated');
    for (let i = 0; i < chunkLen; i++) out.push(data[idx + 1 + i]);
    idx += code;
    if (code < 0xFF && idx < data.length) out.push(0);
  }
  return new Uint8Array(out);
}

function crc16(data) {
  let crc = 0xFFFF;
  for (let i = 0; i < data.length; i++) {
    crc ^= (data[i] << 8);
    for (let bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
      else crc = (crc << 1) & 0xFFFF;
    }
  }
  return crc;
}

const CBOR = (() => {
  function encodeUint(n, major) {
    major = major || 0;
    if (n < 24) return [major << 5 | n];
    if (n < 256) return [major << 5 | 24, n];
    if (n < 65536) return [major << 5 | 25, (n >> 8) & 0xFF, n & 0xFF];
    if (n < 4294967296) {
      return [major << 5 | 26, (n >>> 24) & 0xFF, (n >>> 16) & 0xFF, (n >>> 8) & 0xFF, n & 0xFF];
    }
    // 8-byte form, for a value above 2^32 (e.g. a timestamp after about
    // 71.6 minutes of uptime). JS numbers hold this exactly up to 2^53.
    const hi = Math.floor(n / 4294967296);
    const lo = n % 4294967296;
    return [major << 5 | 27,
      (hi >>> 24) & 0xFF, (hi >>> 16) & 0xFF, (hi >>> 8) & 0xFF, hi & 0xFF,
      (lo >>> 24) & 0xFF, (lo >>> 16) & 0xFF, (lo >>> 8) & 0xFF, lo & 0xFF];
  }

  function encodeItem(x) {
    if (x === null || x === undefined) return [0xF6];
    if (typeof x === 'boolean') return [x ? 0xF5 : 0xF4];
    if (typeof x === 'number') {
      if (x >= 0) return encodeUint(x, 0);
      return encodeUint(-x - 1, 1);
    }
    if (typeof x === 'string') {
      const bytes = new TextEncoder().encode(x);
      return [...encodeUint(bytes.length, 3), ...bytes];
    }
    if (x instanceof Uint8Array) {
      return [...encodeUint(x.length, 2), ...x];
    }
    if (Array.isArray(x)) return encodeArray(x);
    throw new Error('CBOR.encode: unsupported value ' + JSON.stringify(x));
  }

  function encodeArray(items) {
    return [...encodeUint(items.length, 4), ...items.flatMap(encodeItem)];
  }

  function encode(items) {
    return new Uint8Array(encodeArray(items));
  }

  function decodeLen(data, offset, info) {
    if (info < 24) return [info, offset];
    if (info === 24) return [data[offset], offset + 1];
    if (info === 25) return [(data[offset] << 8) | data[offset + 1], offset + 2];
    if (info === 26) {
      return [((data[offset] << 24) | (data[offset + 1] << 16) | (data[offset + 2] << 8) | data[offset + 3]) >>> 0, offset + 4];
    }
    if (info === 27) {
      let hi = (data[offset] << 24 | data[offset + 1] << 16 | data[offset + 2] << 8 | data[offset + 3]) >>> 0;
      let lo = (data[offset + 4] << 24 | data[offset + 5] << 16 | data[offset + 6] << 8 | data[offset + 7]) >>> 0;
      return [hi * 4294967296 + lo, offset + 8];
    }
    throw new Error('CBOR.decode: unsupported length info ' + info);
  }

  function decodeItem(data, offset) {
    const byte = data[offset];
    const major = byte >> 5;
    const info = byte & 0x1F;
    offset++;
    if (major === 0) {
      const [n, next] = decodeLen(data, offset, info);
      return [n, next];
    }
    if (major === 1) {
      const [n, next] = decodeLen(data, offset, info);
      return [-1 - n, next];
    }
    if (major === 2) {
      const [len, next] = decodeLen(data, offset, info);
      return [data.slice(next, next + len), next + len];
    }
    if (major === 3) {
      const [len, next] = decodeLen(data, offset, info);
      const bytes = data.slice(next, next + len);
      return [new TextDecoder().decode(bytes), next + len];
    }
    if (major === 4) {
      const [len, next] = decodeLen(data, offset, info);
      let pos = next;
      const items = [];
      for (let i = 0; i < len; i++) {
        const [v, nextPos] = decodeItem(data, pos);
        items.push(v);
        pos = nextPos;
      }
      return [items, pos];
    }
    if (major === 7) {
      if (info === 20) return [false, offset];
      if (info === 21) return [true, offset];
      if (info === 22) return [null, offset];
      throw new Error('CBOR.decode: unsupported simple value ' + info);
    }
    throw new Error('CBOR.decode: unsupported major type ' + major);
  }

  function decode(data) {
    const [v] = decodeItem(data, 0);
    return v;
  }

  return { encode, decode };
})();

/* =========================================================================
 * Self-test -- golden hex vectors, computed from the same algorithms above,
 * verified against the worked examples in docs/06-cbor-encoding.md and
 * against known-good output from components/device_proto. A failure here
 * disables Connect. Every panel runs this at boot.
 * ========================================================================= */

function hexToBytes(hex) {
  const parts = hex.trim().split(/\s+/);
  return new Uint8Array(parts.map((p) => parseInt(p, 16)));
}
function bytesToHex(bytes) {
  return Array.from(bytes).map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
}
function bytesEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}
function deepEqual(a, b) {
  if (Array.isArray(a) && Array.isArray(b)) {
    if (a.length !== b.length) return false;
    for (let i = 0; i < a.length; i++) if (!deepEqual(a[i], b[i])) return false;
    return true;
  }
  return a === b;
}

function runSelfTest() {
  const results = [];
  function check(name, fn) {
    try {
      const pass = fn();
      results.push({ name, pass: !!pass });
    } catch (e) {
      results.push({ name, pass: false, error: String(e) });
    }
  }

  // Vector 1: PING command. docs/06-cbor-encoding.md
  const pingPayload = hexToBytes('83 00 01 64 50 49 4E 47');
  const pingWire = hexToBytes('02 83 09 01 64 50 49 4E 47 31 57 00');
  check('cbor encode: PING payload', () => bytesEqual(CBOR.encode([0, 1, 'PING']), pingPayload));
  check('cbor decode: PING payload', () => deepEqual(CBOR.decode(pingPayload), [0, 1, 'PING']));
  check('crc16: PING payload', () => crc16(pingPayload) === 0x3157);
  check('cobs+crc+delimiter: PING wire frame', () => {
    const framed = new Uint8Array([...pingPayload, 0x31, 0x57]);
    const encoded = cobsEncode(framed);
    const wire = new Uint8Array([...encoded, 0x00]);
    return bytesEqual(wire, pingWire);
  });
  check('cobs decode round trip: PING wire frame', () => {
    const wireNoDelim = pingWire.slice(0, pingWire.length - 1);
    const framed = cobsDecode(wireNoDelim);
    const payload = framed.slice(0, framed.length - 2);
    return bytesEqual(payload, pingPayload);
  });

  // Vector 2: a DATA sample with a dotted channel name. docs/06-cbor-encoding.md
  const samplePayload = hexToBytes('86 04 0C 1A 00 03 BD 08 69 74 6F 66 30 2E 64 69 73 74 19 05 F3 00');
  check('cbor encode: DATA sample payload', () =>
    bytesEqual(CBOR.encode([4, 12, 245000, 'tof0.dist', 1523, 0]), samplePayload));
  check('cbor decode: DATA sample payload', () =>
    deepEqual(CBOR.decode(samplePayload), [4, 12, 245000, 'tof0.dist', 1523, 0]));
  check('crc16: DATA sample payload', () => crc16(samplePayload) === 0xDFC3);

  // A t_us value above 2^32 forces CBOR's 8-byte (0x1B) unsigned-int form.
  // This is the case that hides for the first ~71.6 minutes of uptime.
  check('cbor round trip: uint above 2^32', () => {
    const big = 5000000000; // > 2^32
    const encoded = CBOR.encode([big]);
    return encoded[1] === 0x1B && deepEqual(CBOR.decode(encoded), [big]);
  });

  check('cbor round trip: negative int (offset_mm)', () => {
    const encoded = CBOR.encode([-37]);
    return deepEqual(CBOR.decode(encoded), [-37]);
  });

  check('cbor round trip: null', () => deepEqual(CBOR.decode(CBOR.encode([null])), [null]));
  check('cbor round trip: boolean', () => deepEqual(CBOR.decode(CBOR.encode([true, false])), [true, false]));

  return results;
}

/* =========================================================================
 * S3: Transport -- Link, a thin promise-per-command layer over a Web
 * Serial SerialPort (or a MockSerialPort; see mock-core.js).
 * ========================================================================= */

class ProtocolError extends Error {
  constructor(verb, code) {
    super(verb + ' failed: ' + code + ' (' + (ERROR_MEANING[code] || 'unknown code') + ')');
    this.verb = verb;
    this.code = code;
  }
}

class Link extends EventTarget {
  constructor(port) {
    super();
    this.port = port;
    this.seq = 0;
    this.pending = new Map();
    this.accum = new Uint8Array(MAX_WIRE_FRAME);
    this.accumLen = 0;
    this.closed = false;
    this.stats = { framesRx: 0, framesTx: 0, crcFail: 0, decodeFail: 0 };
  }

  async open() {
    await this.port.open({ baudRate: 115200, bufferSize: 4096 });
    this._attach();
  }

  _attach() {
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this._readLoopDone = this._readLoop();
  }

  async _readLoop() {
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value) this._feed(value);
      }
    } catch (e) {
      // Cancellation during close() lands here; nothing to do.
    }
  }

  _feed(chunk) {
    for (let i = 0; i < chunk.length; i++) {
      if (this.accumLen < this.accum.length) {
        this.accum[this.accumLen++] = chunk[i];
      }
      if (chunk[i] !== 0x00) continue;

      this.stats.framesRx++;
      const wireLen = this.accumLen - 1;
      if (wireLen > 0 && this.accumLen <= this.accum.length) {
        this._decodeFrame(this.accum.slice(0, wireLen));
      } else {
        this.stats.crcFail++;
        this.dispatchEvent(new CustomEvent('framingerror', { detail: { reason: 'oversize' } }));
      }
      this.accumLen = 0;
    }
  }

  _decodeFrame(wireBytes) {
    let framed;
    try {
      framed = cobsDecode(wireBytes);
    } catch (e) {
      this.stats.crcFail++;
      this.dispatchEvent(new CustomEvent('framingerror', { detail: { reason: 'cobs: ' + e.message } }));
      return;
    }
    if (framed.length < 2) {
      this.stats.crcFail++;
      this.dispatchEvent(new CustomEvent('framingerror', { detail: { reason: 'too short' } }));
      return;
    }
    const payload = framed.slice(0, framed.length - 2);
    const receivedCrc = (framed[framed.length - 2] << 8) | framed[framed.length - 1];
    if (crc16(payload) !== receivedCrc) {
      this.stats.crcFail++;
      this.dispatchEvent(new CustomEvent('framingerror', { detail: { reason: 'crc mismatch' } }));
      return;
    }
    let item;
    try {
      item = CBOR.decode(payload);
    } catch (e) {
      this.stats.decodeFail++;
      this.dispatchEvent(new CustomEvent('framingerror', { detail: { reason: 'cbor: ' + e.message } }));
      return;
    }
    this._dispatch(item);
  }

  _dispatch(item) {
    if (!Array.isArray(item) || item.length < 2) return;
    const kind = item[0];
    if (kind === FRAME_KIND.DATA) {
      const [, seq, tUs, channel, value, flags] = item;
      this.dispatchEvent(new CustomEvent('sample', { detail: { seq, tUs, channel, value, flags } }));
      return;
    }
    const seq = item[1];
    const entry = this.pending.get(seq);
    if (!entry) return;
    if (kind === FRAME_KIND.ROW) {
      entry.rows.push(item.slice(3));
      return;
    }
    clearTimeout(entry.timer);
    this.pending.delete(seq);
    if (kind === FRAME_KIND.OK) {
      entry.resolve({ verb: item[2], values: item.slice(3), rows: entry.rows });
    } else if (kind === FRAME_KIND.ERR) {
      entry.reject(new ProtocolError(item[2], item[3]));
    }
  }

  nextSeq() {
    this.seq = (this.seq % 65535) + 1;
    return this.seq;
  }

  send(verb, ...args) {
    return this._send(verb, args, CMD_TIMEOUT_MS);
  }

  sendSlow(verb, ...args) {
    return this._send(verb, args, CAL_TIMEOUT_MS);
  }

  _send(verb, args, timeoutMs) {
    const seq = this.nextSeq();
    const payload = CBOR.encode([FRAME_KIND.CMD, seq, verb, ...args]);
    const crc = crc16(payload);
    const framed = new Uint8Array(payload.length + 2);
    framed.set(payload, 0);
    framed[payload.length] = (crc >> 8) & 0xFF;
    framed[payload.length + 1] = crc & 0xFF;
    const encoded = cobsEncode(framed);
    const wire = new Uint8Array(encoded.length + 1);
    wire.set(encoded, 0);
    wire[encoded.length] = 0x00;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(seq);
        reject(new Error(verb + ' timed out waiting for a response'));
      }, timeoutMs);
      this.pending.set(seq, { verb, rows: [], resolve, reject, timer });
      this.writer.write(wire).then(() => { this.stats.framesTx++; }).catch(reject);
    });
  }

  async close() {
    this.closed = true;
    for (const [, entry] of this.pending) {
      clearTimeout(entry.timer);
      entry.reject(new Error('link closed'));
    }
    this.pending.clear();
    // Order matters: cancel() only requests cancellation. The pending
    // reader.read() call must actually settle before the lock is
    // released, or the port can be left in a state where a later open()
    // fails with "The port is already open".
    try { await this.reader.cancel(); } catch (e) {}
    try { await this._readLoopDone; } catch (e) {}
    try { this.reader.releaseLock(); } catch (e) {}
    try { await this.writer.close(); } catch (e) { try { this.writer.releaseLock(); } catch (_e) {} }
    try { await this.port.close(); } catch (e) {}
  }
}
