// Banana Theme Studio: .zip files, for a theme together with its pictures, fonts and sounds.
//
// write: the files stored as they are (pictures and fonts are already compressed), which any unzip reads.
// read: stored or deflated entries (deflate through the browser's DecompressionStream; Node's zlib in tests).
// A classic script with no dependencies, so it works from file://.
(function (global) {
  "use strict";

  const CRC = (function () {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
    return t;
  })();
  function crc32(b) {
    let c = 0xffffffff;
    for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 255] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }
  const utf8 = (s) => new TextEncoder().encode(s);

  // [{ path, data: Uint8Array | string }] -> Uint8Array (a .zip). Paths use "/"; names are UTF-8 (flag 0x800)
  function write(files, when) {
    const d = when || new Date();
    const time = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
    const date = ((Math.max(1980, d.getFullYear()) - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    const parts = [], central = [];
    let off = 0;
    for (const f of files) {
      const name = utf8(f.path), data = typeof f.data === "string" ? utf8(f.data) : f.data, crc = crc32(data);
      const h = new DataView(new ArrayBuffer(30));
      h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x800, true); h.setUint16(8, 0, true);
      h.setUint16(10, time, true); h.setUint16(12, date, true); h.setUint32(14, crc, true);
      h.setUint32(18, data.length, true); h.setUint32(22, data.length, true); h.setUint16(26, name.length, true); h.setUint16(28, 0, true);
      parts.push(new Uint8Array(h.buffer), name, data);
      const c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x800, true);
      c.setUint16(10, 0, true); c.setUint16(12, time, true); c.setUint16(14, date, true); c.setUint32(16, crc, true);
      c.setUint32(20, data.length, true); c.setUint32(24, data.length, true); c.setUint16(28, name.length, true);
      c.setUint32(38, 0, true); c.setUint32(42, off, true);
      central.push(new Uint8Array(c.buffer), name);
      off += 30 + name.length + data.length;
    }
    const cdSize = central.reduce((n, p) => n + p.length, 0);
    const e = new DataView(new ArrayBuffer(22));
    e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
    e.setUint32(12, cdSize, true); e.setUint32(16, off, true);
    const all = parts.concat(central, [new Uint8Array(e.buffer)]);
    const out = new Uint8Array(all.reduce((n, p) => n + p.length, 0));
    let at = 0;
    for (const p of all) { out.set(p, at); at += p.length; }
    return out;
  }

  async function inflateRaw(data) {
    if (typeof DecompressionStream !== "undefined") {
      const s = new Blob([data]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
      return new Uint8Array(await new Response(s).arrayBuffer());
    }
    if (typeof require === "function") return new Uint8Array(require("zlib").inflateRawSync(data));
    throw new Error("this browser can't unpack compressed zip files");
  }

  // Uint8Array (a .zip) -> [{ path, data: Uint8Array }] (folders left out). Throws on something that isn't one
  async function read(buf) {
    const b = buf instanceof Uint8Array ? buf : new Uint8Array(buf);
    const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
    let end = -1;
    for (let i = b.length - 22; i >= Math.max(0, b.length - 22 - 65535); i--) if (v.getUint32(i, true) === 0x06054b50) { end = i; break; }
    if (end < 0) throw new Error("not a zip file");
    const count = v.getUint16(end + 10, true);
    let p = v.getUint32(end + 16, true);
    const out = [];
    for (let k = 0; k < count; k++) {
      if (v.getUint32(p, true) !== 0x02014b50) throw new Error("a damaged zip file");
      const flags = v.getUint16(p + 8, true), method = v.getUint16(p + 10, true);
      const csize = v.getUint32(p + 20, true), nlen = v.getUint16(p + 28, true), xlen = v.getUint16(p + 30, true), clen = v.getUint16(p + 32, true);
      const lho = v.getUint32(p + 42, true);
      const nameBytes = b.subarray(p + 46, p + 46 + nlen);
      const name = flags & 0x800 ? new TextDecoder().decode(nameBytes) : Array.from(nameBytes, (c) => String.fromCharCode(c)).join("");
      p += 46 + nlen + xlen + clen;
      if (name.endsWith("/")) continue;
      if (flags & 1) throw new Error(name + " is encrypted");
      const start = lho + 30 + v.getUint16(lho + 26, true) + v.getUint16(lho + 28, true);
      const raw = b.subarray(start, start + csize);
      let data;
      if (method === 0) data = raw.slice();
      else if (method === 8) data = await inflateRaw(raw);
      else throw new Error(name + " is packed in a way this can't unpack (method " + method + ")");
      out.push({ path: name.replace(/\\/g, "/"), data: data });
    }
    return out;
  }

  const api = { write, read, crc32 };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.BananaZip = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
