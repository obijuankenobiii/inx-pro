/**
 * Browser-side language-font packer.
 * Uploaded TTF/OTF files are rasterized into the compact .bin format consumed by
 * ExternalFont on the device. This keeps language rendering fast and avoids runtime
 * TTF/OTF parsing on the reader.
 */
(function (global) {
  'use strict';

  var MAGIC = 0x45504446;
  var VERSION = 1;
  /**
   * Reader steps (10–18) use Literata regular advanceY (31…56) as a px baseline, then scaled.
   * ~0.87 was still ~one reader size larger than built-in Literata; ~0.74 aligns steps with system fonts.
   */
  var SIZES = [10, 12, 14, 16, 18];
  var RASTER_CALIBRATION = 0.74;
  // Match fontcon/fontconvert.py 2-bit coverage buckets:
  // coverage 0-111 white, 112-127 light gray, 128-207 dark gray, 208-255 black.
  // This browser path stores from luminance, so the thresholds are inverted.
  var PACK_WHITE_LUM_THRESHOLD = 144;
  var PACK_LIGHT_GRAY_LUM_THRESHOLD = 128;
  var PACK_DARK_GRAY_LUM_THRESHOLD = 48;
  var RASTER_SUPERSAMPLE = 2;
  var GLYPH_YIELD_INTERVAL = 64;
  var GLYPH_YIELD_BUDGET_MS = 60;

  function readerStepToCanvasPx(step) {
    var base;
    switch (step) {
      case 10:
        base = 31;
        break;
      case 12:
        base = 37;
        break;
      case 14:
        base = 43;
        break;
      case 16:
        base = 49;
        break;
      case 18:
        base = 56;
        break;
      default:
        base = (step * 56) / 18;
    }
    return Math.max(8, Math.round(base * RASTER_CALIBRATION));
  }
  /**
   * Codepoint ranges packed into SD .bin fonts (BMP; device uses uint32 CP search).
   * ASCII is intentionally omitted because the reader's built-in fonts handle it;
   * language packs focus on non-ASCII glyphs such as Cyrillic, Greek, and CJK.
   */
  var CP_RANGES = [
    [0x0020, 0x007e],
    [0x00a0, 0x00ff],
    [0x0100, 0x017f],
    [0x0180, 0x024f],
    [0x0300, 0x036f],
    [0x0370, 0x03ff],
    [0x0400, 0x04ff],
    [0x1e00, 0x1eff],
    [0x2000, 0x206f],
    [0x2070, 0x209f],
    [0x2010, 0x203a],
    [0x2040, 0x205f],
    [0x20a0, 0x20cf],
    [0x2100, 0x214f],
    [0x2150, 0x218f],
    [0x2190, 0x21ff],
    [0x2200, 0x22ff],
    [0x2300, 0x23ff],
    [0x2460, 0x24ff],
    [0x2500, 0x257f],
    [0x2580, 0x259f],
    [0x25a0, 0x25ff],
    [0x2600, 0x26ff],
    [0x2700, 0x27bf],
    [0x2b00, 0x2bff],
    [0xfffd, 0xfffd],
  ];
  var MAX_SIDE = 384;
  var MAX_ADVANCE = 256;

  function collectCodepoints(ranges, availableCodepoints) {
    // Prefer the uploaded font's cmap so every supported script is included without
    // hard-coded language ranges. The fixed ranges remain a fallback for unusual
    // font containers whose cmap cannot be read in the browser.
    if (availableCodepoints && availableCodepoints.length) {
      var available = new Set();
      for (var ai = 0; ai < availableCodepoints.length; ai++) {
        var availableCp = availableCodepoints[ai];
        if (availableCp >= 0x20 && availableCp <= 0x7e) continue;
        if (availableCp >= 0x20 && availableCp <= 0x10ffff && !(availableCp >= 0xd800 && availableCp <= 0xdfff)) {
          available.add(availableCp);
        }
      }
      return Array.from(available).sort(function (x, y) {
        return x - y;
      });
    }
    ranges = ranges && ranges.length ? CP_RANGES.concat(ranges) : CP_RANGES;
    var s = new Set();
    for (var ri = 0; ri < ranges.length; ri++) {
      var a = ranges[ri][0];
      var b = ranges[ri][1];
      for (var cp = a; cp <= b; cp++) {
        if (cp >= 0xd800 && cp <= 0xdfff) continue;
        if (cp < 0x20 && cp !== 0x09) continue;
        if (cp >= 0x20 && cp <= 0x7e) continue;
        s.add(cp);
      }
    }
    return Array.from(s).sort(function (x, y) {
      return x - y;
    });
  }

  function readTag(view, offset) {
    return String.fromCharCode(view.getUint8(offset), view.getUint8(offset + 1), view.getUint8(offset + 2),
                               view.getUint8(offset + 3));
  }

  function addCmapFormat4(view, offset, codepoints) {
    var segCount = view.getUint16(offset + 6, false) / 2;
    var endCodes = offset + 14;
    var startCodes = endCodes + segCount * 2 + 2;
    var idDeltas = startCodes + segCount * 2;
    var idRangeOffsets = idDeltas + segCount * 2;
    for (var i = 0; i < segCount; i++) {
      var start = view.getUint16(startCodes + i * 2, false);
      var end = view.getUint16(endCodes + i * 2, false);
      if (start > end || start === 0xffff) continue;
      var delta = view.getInt16(idDeltas + i * 2, false);
      var rangeOffset = view.getUint16(idRangeOffsets + i * 2, false);
      for (var cp = start; cp <= end; cp++) {
        var glyph = 0;
        if (rangeOffset === 0) {
          glyph = (cp + delta) & 0xffff;
        } else {
          var glyphOffset = idRangeOffsets + i * 2 + rangeOffset + (cp - start) * 2;
          if (glyphOffset + 2 <= view.byteLength) glyph = view.getUint16(glyphOffset, false);
          if (glyph !== 0) glyph = (glyph + delta) & 0xffff;
        }
        if (glyph !== 0) codepoints.add(cp);
      }
    }
  }

  function addCmapFormat6(view, offset, codepoints) {
    var firstCode = view.getUint16(offset + 4, false);
    var entryCount = view.getUint16(offset + 6, false);
    for (var i = 0; i < entryCount; i++) {
      if (view.getUint16(offset + 10 + i * 2, false) !== 0) codepoints.add(firstCode + i);
    }
  }

  function addCmapFormat12Or13(view, offset, codepoints) {
    var groupCount = view.getUint32(offset + 12, false);
    for (var i = 0; i < groupCount; i++) {
      var group = offset + 16 + i * 12;
      var start = view.getUint32(group, false);
      var end = view.getUint32(group + 4, false);
      var glyph = view.getUint32(group + 8, false);
      if (start > end || glyph === 0) continue;
      for (var cp = start; cp <= end && cp <= 0x10ffff; cp++) codepoints.add(cp);
    }
  }

  function extractFontCodepoints(buffer) {
    try {
      var view = new DataView(buffer);
      if (view.byteLength < 12) return [];
      var numTables = view.getUint16(4, false);
      var cmapOffset = -1;
      for (var i = 0; i < numTables; i++) {
        var table = 12 + i * 16;
        if (table + 16 > view.byteLength) break;
        if (readTag(view, table) === 'cmap') {
          cmapOffset = view.getUint32(table + 8, false);
          break;
        }
      }
      if (cmapOffset < 0 || cmapOffset + 4 > view.byteLength) return [];
      var subtableCount = view.getUint16(cmapOffset + 2, false);
      var subtables = [];
      for (var si = 0; si < subtableCount; si++) {
        var record = cmapOffset + 4 + si * 8;
        if (record + 8 > view.byteLength) break;
        var platform = view.getUint16(record, false);
        var encoding = view.getUint16(record + 2, false);
        var subOffset = cmapOffset + view.getUint32(record + 4, false);
        if (subOffset + 2 > view.byteLength) continue;
        var format = view.getUint16(subOffset, false);
        var priority = format === 12 || format === 13 ? 3 : format === 4 ? 2 : format === 6 ? 1 : 0;
        if (platform === 3 && encoding === 10) priority += 2;
        if (platform === 0) priority += 1;
        if (priority > 0) subtables.push({offset: subOffset, format: format, priority: priority});
      }
      subtables.sort(function (a, b) { return b.priority - a.priority; });
      var codepoints = new Set();
      for (var ti = 0; ti < subtables.length; ti++) {
        var subtable = subtables[ti];
        if (subtable.format === 4) addCmapFormat4(view, subtable.offset, codepoints);
        else if (subtable.format === 6) addCmapFormat6(view, subtable.offset, codepoints);
        else if (subtable.format === 12 || subtable.format === 13) addCmapFormat12Or13(view, subtable.offset, codepoints);
      }
      return Array.from(codepoints).sort(function (x, y) { return x - y; });
    } catch (_) {
      return [];
    }
  }

  function sanitizeFamilyName(raw) {
    var t = (raw || '').trim();
    if (!t) t = 'CustomFont';
    t = t.replace(/[^a-zA-Z0-9 _\-]/g, '_').replace(/\s+/g, '_');
    if (t.length > 48) t = t.slice(0, 48);
    if (t.startsWith('.')) t = '_' + t;
    return t;
  }

  function grayToStored(L) {
    if (L >= PACK_WHITE_LUM_THRESHOLD) return 0;
    if (L >= PACK_LIGHT_GRAY_LUM_THRESHOLD) return 1;
    if (L >= PACK_DARK_GRAY_LUM_THRESHOLD) return 2;
    return 3;
  }

  function pack2bitLinear(padW, h, getLum) {
    var totalPx = padW * h;
    var nbytes = Math.ceil(totalPx / 4);
    var out = new Uint8Array(nbytes);
    for (var i = 0; i < totalPx; i++) {
      var gy = Math.floor(i / padW);
      var gx = i % padW;
      var stored = grayToStored(getLum(gy, gx));
      var bi = i >> 2;
      var sh = (3 - (i & 3)) << 1;
      out[bi] |= stored << sh;
    }
    return out;
  }

  function pack1bitLinear(padW, h, getLum) {
    var totalPx = padW * h;
    var nbytes = Math.ceil(totalPx / 8);
    var out = new Uint8Array(nbytes);
    for (var i = 0; i < totalPx; i++) {
      var gy = Math.floor(i / padW);
      var gx = i % padW;
      var ink = getLum(gy, gx) < PACK_WHITE_LUM_THRESHOLD;
      if (ink) {
        var bi = i >> 3;
        var sh = 7 - (i & 7);
        out[bi] |= 1 << sh;
      }
    }
    return out;
  }

  function fontSpecPx(family, canvasPx) {
    return canvasPx + 'px "' + family + '"';
  }

  function nowMs() {
    return global.performance && typeof global.performance.now === 'function' ? global.performance.now() : Date.now();
  }

  function yieldToBrowser() {
    return new Promise(function (resolve) {
      setTimeout(resolve, 0);
    });
  }

  function createRasterScratch() {
    var sample = RASTER_SUPERSAMPLE;
    var c = document.createElement('canvas');
    c.width = 512 * sample;
    c.height = 512 * sample;
    return {
      canvas: c,
      ctx: c.getContext('2d'),
      width: c.width,
      height: c.height,
    };
  }

  function measureRef(ctx, family, readerStep) {
    var px = readerStepToCanvasPx(readerStep);
    ctx.textBaseline = 'alphabetic';
    ctx.font = fontSpecPx(family, px);
    var m = ctx.measureText('|');
    var asc = m.actualBoundingBoxAscent;
    var desc = m.actualBoundingBoxDescent;
    if (!isFinite(asc) || asc <= 0) asc = px * 0.72;
    if (!isFinite(desc) || desc <= 0) desc = px * 0.28;
    var lh = Math.round(asc + desc);
    return {
      lineHeight: lh,
      ascender: Math.round(asc),
      descender: Math.round(desc),
    };
  }

  function rasterizeChar(family, readerStep, cp, scratch, oneBit) {
    var px = readerStepToCanvasPx(readerStep);
    var sample = RASTER_SUPERSAMPLE;
    var W = scratch ? scratch.width : 512 * sample;
    var H = scratch ? scratch.height : 512 * sample;
    var ox = 200;
    var by = 300;
    var c = scratch ? scratch.canvas : document.createElement('canvas');
    if (!scratch) {
      c.width = W;
      c.height = H;
    }
    var ctx = scratch ? scratch.ctx : c.getContext('2d');
    ctx.textBaseline = 'alphabetic';
    ctx.font = fontSpecPx(family, px * sample);
    var ch = String.fromCodePoint(cp);
    var m = ctx.measureText(ch);
    var adv = Math.round(m.width / sample);
    if (adv < 1) adv = 1;
    if (adv > MAX_ADVANCE) adv = MAX_ADVANCE;
    var topRef = Math.round((m.actualBoundingBoxAscent > 0 ? m.actualBoundingBoxAscent : px * sample * 0.72) / sample);

    if (cp === 0x20 || cp === 0xa0 || ch === '\t') {
      if (cp === 0x20 || cp === 0xa0) adv = Math.max(adv, Math.round(px * 0.35));
      return { w: 0, h: 0, left: 0, top: topRef, adv: adv, bits: new Uint8Array(0) };
    }

    var metricLeft = isFinite(m.actualBoundingBoxLeft) && m.actualBoundingBoxLeft > 0 ? m.actualBoundingBoxLeft : 0;
    var metricRight = isFinite(m.actualBoundingBoxRight) && m.actualBoundingBoxRight > 0 ? m.actualBoundingBoxRight : Math.max(adv * sample, px * sample * 0.5);
    var metricAsc = isFinite(m.actualBoundingBoxAscent) && m.actualBoundingBoxAscent > 0 ? m.actualBoundingBoxAscent : px * sample * 0.9;
    var metricDesc = isFinite(m.actualBoundingBoxDescent) && m.actualBoundingBoxDescent > 0 ? m.actualBoundingBoxDescent : px * sample * 0.35;
    var cropPad = 6 * sample;
    var cropX = Math.max(0, Math.floor(ox * sample - metricLeft - cropPad));
    var cropY = Math.max(0, Math.floor(by * sample - metricAsc - cropPad));
    var cropRight = Math.min(W, Math.ceil(ox * sample + metricRight + cropPad));
    var cropBottom = Math.min(H, Math.ceil(by * sample + metricDesc + cropPad));
    var cropW = Math.max(1, cropRight - cropX);
    var cropH = Math.max(1, cropBottom - cropY);

    ctx.fillStyle = '#ffffff';
    ctx.fillRect(cropX, cropY, cropW, cropH);
    ctx.fillStyle = '#000000';
    ctx.fillText(ch, ox * sample, by * sample);
    var id = ctx.getImageData(cropX, cropY, cropW, cropH).data;
    var minX = cropW,
      minY = cropH,
      maxX = -1,
      maxY = -1;
    var thr = 248;
    for (var y = 0; y < cropH; y++) {
      for (var x = 0; x < cropW; x++) {
        var j = (y * cropW + x) * 4;
        var L = 0.299 * id[j] + 0.587 * id[j + 1] + 0.114 * id[j + 2];
        if (L < thr) {
          if (x < minX) minX = x;
          if (y < minY) minY = y;
          if (x > maxX) maxX = x;
          if (y > maxY) maxY = y;
        }
      }
    }
    if (maxX < minX) {
      return { w: 0, h: 0, left: 0, top: topRef, adv: adv, bits: new Uint8Array(0) };
    }
    var minOutX = Math.floor((cropX + minX) / sample);
    var minOutY = Math.floor((cropY + minY) / sample);
    var maxOutX = Math.ceil((cropX + maxX + 1) / sample) - 1;
    var maxOutY = Math.ceil((cropY + maxY + 1) / sample) - 1;
    var bw = maxOutX - minOutX + 1;
    var bh = maxOutY - minOutY + 1;
    if (bw > MAX_SIDE || bh > MAX_SIDE) {
      return null;
    }
    var padW = oneBit ? (bw + 7) & ~7 : (bw + 3) & ~3;
    var getLum = function (gy, gx) {
      if (gx >= bw) return 255;
      var sx0 = (minOutX + gx) * sample - cropX;
      var sy0 = (minOutY + gy) * sample - cropY;
      var ink = 0;
      var count = 0;
      for (var yy = 0; yy < sample; yy++) {
        for (var xx = 0; xx < sample; xx++) {
          var sx = sx0 + xx;
          var sy = sy0 + yy;
          if (sx < 0 || sy < 0 || sx >= cropW || sy >= cropH) continue;
          var j = (sy * cropW + sx) * 4;
          var L = 0.299 * id[j] + 0.587 * id[j + 1] + 0.114 * id[j + 2];
          ink += 255 - L;
          count++;
        }
      }
      return count > 0 ? 255 - ink / count : 255;
    };
    var bits = oneBit ? pack1bitLinear(padW, bh, getLum) : pack2bitLinear(padW, bh, getLum);
    return {
      w: padW,
      h: bh,
      left: minOutX - ox,
      top: by - minOutY,
      adv: adv,
      bits: bits,
    };
  }

  function writeUint16(dv, o, v) {
    dv.setUint16(o, v, true);
  }
  function writeInt16(dv, o, v) {
    dv.setInt16(o, v, true);
  }
  function writeUint32(dv, o, v) {
    dv.setUint32(o, v, true);
  }

  /**
   * @param {string} styleName — "Regular" | "Bold" | "Italic" | "BoldItalic" (embedded name + filename stem)
   * @param {string} familyCss — loaded @font-face family string
   * @param {number} readerStep — 10|12|14|16|18 (filename suffix; canvas px from readerStepToCanvasPx)
   * @param {number[]} codepoints sorted ascending
   */
  async function buildBin(styleName, familyCss, readerStep, codepoints, callbacks) {
    callbacks = callbacks || {};
    var onGlyphProgress = callbacks.onGlyphProgress || function () {};
    var oneBit = !!callbacks.oneBit;
    var refC = document.createElement('canvas');
    refC.width = 256;
    refC.height = 128;
    var rctx = refC.getContext('2d');
    var ref = measureRef(rctx, familyCss, readerStep);

    var rows = [];
    var bitmapChunks = [];
    var cum = 0;
    var scratch = createRasterScratch();
    var lastYieldAt = nowMs();

    for (var i = 0; i < codepoints.length; i++) {
      var cp = codepoints[i];
      var g = rasterizeChar(familyCss, readerStep, cp, scratch, oneBit);
      if (g === null) continue;
      var dlen = g.bits.length;
      if (g.w > MAX_SIDE || g.h > MAX_SIDE) continue;
      var ax = g.adv;
      if (ax > MAX_ADVANCE) ax = MAX_ADVANCE;
      var row = new ArrayBuffer(24);
      var dv = new DataView(row);
      writeUint16(dv, 0, g.w);
      writeUint16(dv, 2, g.h);
      writeUint16(dv, 4, ax);
      writeInt16(dv, 6, g.left);
      writeInt16(dv, 8, g.top);
      writeUint32(dv, 10, dlen);
      writeUint32(dv, 14, cum);
      writeUint32(dv, 18, cp >>> 0);
      writeUint16(dv, 22, 0);
      rows.push(new Uint8Array(row));
      if (dlen) bitmapChunks.push(g.bits);
      cum += dlen;

      if (((i + 1) % GLYPH_YIELD_INTERVAL) === 0 || nowMs() - lastYieldAt >= GLYPH_YIELD_BUDGET_MS) {
        onGlyphProgress(i + 1, codepoints.length);
        await yieldToBrowser();
        lastYieldAt = nowMs();
      }
    }
    onGlyphProgress(codepoints.length, codepoints.length);

    if (!rows.length) {
      throw new Error('No glyphs generated for ' + styleName + ' at reader step ' + readerStep);
    }

    var enc = new TextEncoder();
    var nameUtf8 = enc.encode(styleName);
    var nameLen = nameUtf8.length;
    if (nameLen > 255) throw new Error('Style name too long');

    var glyphCount = rows.length;
    var headerSize = 4 + 4 + 2 + nameLen + 2 + 2 + 2 + 1 + 2 + 4;
    var tableBytes = glyphCount * 24;
    var bitmapStart = headerSize + tableBytes;
    var totalSize = bitmapStart + cum;
    var out = new Uint8Array(totalSize);
    var dv = new DataView(out.buffer);
    var o = 0;
    writeUint32(dv, o, MAGIC);
    o += 4;
    writeUint32(dv, o, VERSION);
    o += 4;
    writeUint16(dv, o, nameLen);
    o += 2;
    out.set(nameUtf8, o);
    o += nameLen;
    writeInt16(dv, o, ref.lineHeight);
    o += 2;
    writeInt16(dv, o, ref.ascender);
    o += 2;
    writeInt16(dv, o, ref.descender);
    o += 2;
    out[o++] = oneBit ? 0 : 1;
    writeUint16(dv, o, 0);
    o += 2;
    writeUint32(dv, o, glyphCount);
    o += 4;
    for (var ri = 0; ri < rows.length; ri++) {
      out.set(rows[ri], o);
      o += 24;
    }
    for (var bi = 0; bi < bitmapChunks.length; bi++) {
      out.set(bitmapChunks[bi], o);
      o += bitmapChunks[bi].length;
    }
    return out;
  }

  async function registerFace(uniqueFamily, blob, descriptors) {
    var buf = await blob.arrayBuffer();
    var face = new FontFace(uniqueFamily, buf, descriptors || {});
    await face.load();
    document.fonts.add(face);
    return face;
  }

  /**
   * @returns {{ filename: string, blob: Blob }[]}
   */
  async function buildAllBins(opts) {
    var regular = opts.regular;
    var bold = opts.bold;
    var italic = opts.italic;
    var boldItalic = opts.boldItalic;
    var oneBit = !!opts.oneBit;
    var uploadedCodepoints = extractFontCodepoints(await regular.arrayBuffer());
    var codepoints = collectCodepoints(opts.codepointRanges, uploadedCodepoints);
    var onLog = opts.onLog || function () {};
    var onProgress = opts.onProgress || function () {};

    if (!regular) throw new Error('Regular TTF/OTF is required');
    var jobs = [
      { key: 'Regular', blob: regular },
      { key: 'Bold', blob: bold },
      { key: 'Italic', blob: italic },
      { key: 'BoldItalic', blob: boldItalic },
    ].filter(function (job) {
      return !!job.blob;
    });
    var outFonts = [];
    var familyPrefix = 'InxLanguageFont_' + Date.now().toString(36) + '_' + Math.random().toString(36).slice(2);
    for (var i = 0; i < jobs.length; i++) {
      var job = jobs[i];
      var familyCss = familyPrefix + '_' + job.key;
      await registerFace(familyCss, job.blob);
      for (var si = 0; si < SIZES.length; si++) {
        var readerStep = SIZES[si];
        var styleName = job.key + '_' + readerStep;
        onProgress(i * SIZES.length + si + 1, jobs.length * SIZES.length, styleName, 'raster');
        onLog('Rasterizing ' + styleName + ' (' + (oneBit ? '1-bit' : '2-bit') + ', ' + codepoints.length + ' glyphs)…', 'info');
        var bytes = await buildBin(styleName, familyCss, readerStep, codepoints, {
          oneBit: oneBit,
          onGlyphProgress: function (done, total) {
            var completedStyles = i * SIZES.length + si;
            onProgress(completedStyles + (total ? done / total : 0), jobs.length * SIZES.length, styleName, 'glyph');
          }
        });
        outFonts.push({
          filename: styleName + '.bin',
          blob: new Blob([bytes], {type: 'application/octet-stream'})
        });
      }
      var face = Array.from(document.fonts).find(function (candidate) { return candidate.family === familyCss; });
      if (face) document.fonts.delete(face);
    }
    return outFonts;
  }

  global.InxFontPack = {
    MAGIC: MAGIC,
    SIZES: SIZES,
    readerStepToCanvasPx: readerStepToCanvasPx,
    sanitizeFamilyName: sanitizeFamilyName,
    buildAllBins: buildAllBins,
  };
})(typeof window !== 'undefined' ? window : globalThis);
