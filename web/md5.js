"use strict";
// RFC 1321 MD5 for transfer integrity, not authentication. Web Crypto does not
// offer MD5 and is unavailable at a controller's plain-HTTP local address.
window.firmwareMd5 = (bytes) => {
  const padded = new Uint8Array(Math.ceil((bytes.length + 9) / 64) * 64);
  padded.set(bytes);
  padded[bytes.length] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 8, (bytes.length * 8) >>> 0, true);
  view.setUint32(padded.length - 4, Math.floor(bytes.length / 0x20000000), true);
  const shift = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
  const constants = Array.from({ length: 64 }, (_, i) =>
    Math.floor(Math.abs(Math.sin(i + 1)) * 0x100000000) | 0,
  );
  const hash = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476];
  for (let offset = 0; offset < padded.length; offset += 64) {
    let [a, b, c, d] = hash;
    for (let i = 0; i < 64; i++) {
      let f, g;
      if (i < 16) { f = (b & c) | (~b & d); g = i; }
      else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
      else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
      else { f = c ^ (b | ~d); g = (7 * i) % 16; }
      const word = (a + f + constants[i] + view.getUint32(offset + g * 4, true)) | 0;
      const bits = shift[(i >>> 4) * 4 + (i % 4)];
      [a, b, c, d] = [d, (b + ((word << bits) | (word >>> (32 - bits)))) | 0, b, c];
    }
    [a, b, c, d].forEach((value, i) => { hash[i] = (hash[i] + value) | 0; });
  }
  return hash.map((word) => [0, 8, 16, 24]
    .map((bits) => ((word >>> bits) & 255).toString(16).padStart(2, "0")).join("")).join("");
};
