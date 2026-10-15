// SPDX-License-Identifier: GPL-2.0-or-later
//
// Renders a Dolphin Duo canvas snapshot (the JSON `dolphin-tool duo preview --json` prints) onto
// an HTML canvas. This mirrors DuoCanvasRenderer.kt so companions can be designed and checked on
// desktop; keep the two in step.
//
// Colours are Android-style: "#RGB", "#RRGGBB" or "#AARRGGBB" (alpha first).

(function (global) {
  'use strict';

  function parseColor(text) {
    if (!text || typeof text !== 'string') return null;
    let v = text.trim();
    if (!v.startsWith('#')) return v; // CSS names pass through.
    v = v.slice(1);
    if (v.length === 3) v = v.split('').map((c) => c + c).join('');
    if (v.length === 6) return '#' + v;
    if (v.length === 8) {
      const a = parseInt(v.slice(0, 2), 16) / 255;
      const r = parseInt(v.slice(2, 4), 16);
      const g = parseInt(v.slice(4, 6), 16);
      const b = parseInt(v.slice(6, 8), 16);
      return `rgba(${r},${g},${b},${a.toFixed(3)})`;
    }
    return null;
  }

  function transparentOf(css) {
    // Same hue, zero alpha (for sheen gradients).
    const probe = document.createElement('canvas').getContext('2d');
    probe.fillStyle = css;
    const s = probe.fillStyle;
    if (s.startsWith('#')) {
      const r = parseInt(s.slice(1, 3), 16), g = parseInt(s.slice(3, 5), 16), b = parseInt(s.slice(5, 7), 16);
      return `rgba(${r},${g},${b},0)`;
    }
    return s.replace(/rgba?\(([^,]+),([^,]+),([^,)]+)(,[^)]+)?\)/, 'rgba($1,$2,$3,0)');
  }

  const num = (e, k, d) => (e[k] === undefined || e[k] === null || Number.isNaN(+e[k]) ? d : +e[k]);
  const col = (e, k) => parseColor(e[k]);
  const box = (e) => ({ x: num(e, 'x', 0), y: num(e, 'y', 0), w: num(e, 'w', 0), h: num(e, 'h', 0) });
  const inset = (r, by) => ({ x: r.x + by, y: r.y + by, w: r.w - by * 2, h: r.h - by * 2 });

  function roundRect(ctx, r, radius) {
    ctx.beginPath();
    const rr = Math.max(0, Math.min(radius, r.w / 2, r.h / 2));
    ctx.roundRect ? ctx.roundRect(r.x, r.y, r.w, r.h, rr) : ctx.rect(r.x, r.y, r.w, r.h);
  }

  function framePath(ctx, r, cut, radius) {
    if (cut > 0) {
      const k = Math.min(cut, Math.min(r.w, r.h) / 2);
      ctx.beginPath();
      ctx.moveTo(r.x + k, r.y);
      ctx.lineTo(r.x + r.w - k, r.y);
      ctx.lineTo(r.x + r.w, r.y + k);
      ctx.lineTo(r.x + r.w, r.y + r.h - k);
      ctx.lineTo(r.x + r.w - k, r.y + r.h);
      ctx.lineTo(r.x + k, r.y + r.h);
      ctx.lineTo(r.x, r.y + r.h - k);
      ctx.lineTo(r.x, r.y + k);
      ctx.closePath();
    } else {
      roundRect(ctx, r, radius);
    }
  }

  function diamond(ctx, cx, cy, r) {
    ctx.beginPath();
    ctx.moveTo(cx, cy - r);
    ctx.lineTo(cx + r, cy);
    ctx.lineTo(cx, cy + r);
    ctx.lineTo(cx - r, cy);
    ctx.closePath();
  }

  const corners = (r) => [[r.x, r.y], [r.x + r.w, r.y], [r.x, r.y + r.h], [r.x + r.w, r.y + r.h]];

  function fillStyle(ctx, e, r, horizontal) {
    const top = col(e, 'fill');
    if (!top) return null;
    const bottom = col(e, 'fill2');
    if (!bottom) return top;
    const g = horizontal ? ctx.createLinearGradient(r.x, 0, r.x + r.w, 0) : ctx.createLinearGradient(0, r.y, 0, r.y + r.h);
    g.addColorStop(0, top);
    g.addColorStop(1, bottom);
    return g;
  }

  function fontFor(e, info, size) {
    const name = e.font || 'serif';
    const family = info.fonts && info.fonts[name] ? `duo-${name}` : name === 'sans' ? 'sans-serif' : name === 'mono' ? 'monospace' : 'serif';
    return `${e.weight === 'bold' ? 'bold ' : ''}${size}px "${family}", serif`;
  }

  const drawers = {
    rect(ctx, e) {
      const r = box(e);
      const radius = num(e, 'radius', 0);
      const f = fillStyle(ctx, e, r);
      if (f) { roundRect(ctx, r, radius); ctx.fillStyle = f; ctx.fill(); }
      const s = col(e, 'stroke');
      if (s) {
        const w = num(e, 'stroke_width', 1);
        roundRect(ctx, inset(r, w / 2), radius);
        ctx.strokeStyle = s; ctx.lineWidth = w; ctx.stroke();
      }
    },

    frame(ctx, e) {
      const r = box(e);
      const corner = e.corner || 'notch';
      const size = num(e, 'corner_size', 14);
      const radius = num(e, 'radius', 0);
      const cut = corner === 'notch' ? size : 0;
      const f = fillStyle(ctx, e, r);
      if (f) { framePath(ctx, r, cut, radius); ctx.fillStyle = f; ctx.fill(); }
      const glow = col(e, 'glow');
      if (glow) {
        ctx.save();
        framePath(ctx, r, cut, radius);
        ctx.clip();
        const g = ctx.createLinearGradient(0, r.y, 0, r.y + r.h * 0.45);
        g.addColorStop(0, glow);
        g.addColorStop(1, transparentOf(glow));
        ctx.fillStyle = g;
        ctx.fillRect(r.x, r.y, r.w, r.h);
        ctx.restore();
      }
      const edge = col(e, 'edge');
      if (edge) {
        const w = num(e, 'stroke_width', 2);
        framePath(ctx, inset(r, w / 2), cut, radius);
        ctx.strokeStyle = edge; ctx.lineWidth = w; ctx.stroke();
      }
      const trim = col(e, 'trim');
      if (!trim) return;
      const ins = num(e, 'inset', 5);
      const tw = num(e, 'trim_width', 1.5);
      const inner = inset(r, ins);
      const innerCut = corner === 'notch' ? Math.max(0, size - ins * 0.6) : 0;
      framePath(ctx, inner, innerCut, Math.max(0, radius - ins));
      ctx.strokeStyle = trim; ctx.lineWidth = tw; ctx.stroke();
      ctx.fillStyle = trim;
      const cx = inner.x + inner.w / 2, cy = inner.y + inner.h / 2;
      if (corner === 'diamond') {
        for (const [x, y] of corners(inner)) { diamond(ctx, x, y, size * 0.35); ctx.fill(); }
      } else if (corner === 'notch') {
        for (const [x, y] of corners(inner)) {
          const ox = x < cx ? innerCut * 0.5 : -innerCut * 0.5;
          const oy = y < cy ? innerCut * 0.5 : -innerCut * 0.5;
          diamond(ctx, x + ox, y + oy, size * 0.22); ctx.fill();
        }
      } else if (corner === 'bracket') {
        ctx.lineWidth = tw * 2;
        for (const [x, y] of corners(inner)) {
          const sx = x < cx ? 1 : -1, sy = y < cy ? 1 : -1;
          ctx.beginPath(); ctx.moveTo(x + sx * size, y); ctx.lineTo(x, y); ctx.lineTo(x, y + sy * size); ctx.stroke();
        }
      }
    },

    text(ctx, e, info) {
      let value = e.text == null ? '' : String(e.text);
      if (!value) return;
      if (e.case === 'upper') value = value.toUpperCase();
      const r = box(e);
      let size = num(e, 'size', r.h * 0.7);
      ctx.font = fontFor(e, info, size);
      const spacing = num(e, 'letter_spacing', 0);
      if ('letterSpacing' in ctx) ctx.letterSpacing = `${spacing * size}px`;
      if (num(e, 'fit', 0) && r.w > 0) {
        const min = num(e, 'min_size', size * 0.5);
        while (ctx.measureText(value).width > r.w && size > min) { size *= 0.95; ctx.font = fontFor(e, info, size); if ('letterSpacing' in ctx) ctx.letterSpacing = `${spacing * size}px`; }
        if (ctx.measureText(value).width > r.w) {
          while (value.length > 1 && ctx.measureText(value + '…').width > r.w) value = value.slice(0, -1);
          value += '…';
        }
      }
      const width = ctx.measureText(value).width;
      const align = e.align || 'left';
      const x = align === 'center' ? r.x + r.w / 2 - width / 2 : align === 'right' ? r.x + r.w - width : r.x;
      const m = ctx.measureText('Hg');
      const ascent = m.fontBoundingBoxAscent ?? size * 0.8;
      const descent = m.fontBoundingBoxDescent ?? size * 0.2;
      const valign = e.valign || 'middle';
      const baseline = valign === 'top' ? r.y + ascent : valign === 'bottom' ? r.y + r.h - descent : r.y + r.h / 2 + (ascent - descent) / 2;
      ctx.textBaseline = 'alphabetic';
      const shadow = col(e, 'shadow');
      if (shadow) { ctx.fillStyle = shadow; ctx.fillText(value, x + num(e, 'shadow_dx', 2), baseline + num(e, 'shadow_dy', 2)); }
      const outline = col(e, 'outline');
      if (outline) {
        ctx.lineJoin = 'round'; ctx.lineWidth = num(e, 'outline_width', 3); ctx.strokeStyle = outline;
        ctx.strokeText(value, x, baseline);
      }
      const top = col(e, 'color') || '#ffffff';
      const bottom = col(e, 'fill2');
      if (bottom) {
        const g = ctx.createLinearGradient(0, baseline - ascent, 0, baseline);
        g.addColorStop(0, top); g.addColorStop(1, bottom);
        ctx.fillStyle = g;
      } else ctx.fillStyle = top;
      ctx.fillText(value, x, baseline);
      if ('letterSpacing' in ctx) ctx.letterSpacing = '0px';
    },

    bar(ctx, e) {
      const r = box(e);
      const radius = num(e, 'radius', r.h / 2);
      const track = col(e, 'track');
      if (track) {
        const t2 = col(e, 'track2');
        let style = track;
        if (t2) { style = ctx.createLinearGradient(0, r.y, 0, r.y + r.h); style.addColorStop(0, track); style.addColorStop(1, t2); }
        roundRect(ctx, r, radius); ctx.fillStyle = style; ctx.fill();
      }
      const fraction = e.fraction;
      if (fraction != null && fraction > 0) {
        ctx.save();
        ctx.beginPath(); ctx.rect(r.x, r.y, r.w * Math.min(1, Math.max(0, fraction)), r.h); ctx.clip();
        const f = fillStyle(ctx, e, r, true);
        if (f) { roundRect(ctx, r, radius); ctx.fillStyle = f; ctx.fill(); }
        ctx.restore();
      }
      const segments = Math.trunc(num(e, 'count', 0));
      if (segments >= 2 && segments <= 200) {
        ctx.strokeStyle = col(e, 'off') || 'rgba(0,0,0,0.4)';
        ctx.lineWidth = num(e, 'gap', 2);
        for (let i = 1; i < segments; i++) {
          const x = r.x + (r.w * i) / segments;
          ctx.beginPath(); ctx.moveTo(x, r.y); ctx.lineTo(x, r.y + r.h); ctx.stroke();
        }
      }
      const s = col(e, 'stroke');
      if (s) { const w = num(e, 'stroke_width', 1); roundRect(ctx, inset(r, w / 2), radius); ctx.strokeStyle = s; ctx.lineWidth = w; ctx.stroke(); }
    },

    pips(ctx, e) {
      const r = box(e);
      const count = Math.max(0, Math.min(100, Math.trunc(num(e, 'count', 0))));
      if (!count) return;
      const filled = num(e, 'filled', 0);
      const gap = num(e, 'gap', r.h * 0.25);
      const size = Math.min(r.h, (r.w - gap * (count - 1)) / count);
      const on = col(e, 'fill') || '#ffffff';
      const off = col(e, 'off') || 'rgba(255,255,255,0.25)';
      for (let i = 0; i < count; i++) {
        const cx = r.x + size / 2 + i * (size + gap), cy = r.y + r.h / 2;
        ctx.fillStyle = i < filled ? on : off;
        const shape = e.shape || 'diamond';
        if (shape === 'circle') { ctx.beginPath(); ctx.arc(cx, cy, size / 2, 0, Math.PI * 2); ctx.fill(); }
        else if (shape === 'square') ctx.fillRect(cx - size / 2, cy - size / 2, size, size);
        else { diamond(ctx, cx, cy, size / 2); ctx.fill(); }
      }
    },

    image(ctx, e, info, assets) {
      const img = assets.images[e.src];
      if (!img) return;
      const r = box(e);
      const tint = col(e, 'tint');
      if (!tint) { ctx.drawImage(img, r.x, r.y, r.w, r.h); return; }
      const off = document.createElement('canvas');
      off.width = Math.max(1, Math.round(r.w)); off.height = Math.max(1, Math.round(r.h));
      const o = off.getContext('2d');
      o.drawImage(img, 0, 0, off.width, off.height);
      o.globalCompositeOperation = 'source-in';
      o.fillStyle = tint; o.fillRect(0, 0, off.width, off.height);
      ctx.drawImage(off, r.x, r.y, r.w, r.h);
    },

    path(ctx, e) {
      const vb = e.viewbox;
      if (!vb || vb[2] <= 0 || vb[3] <= 0 || !e.d) return;
      const r = box(e);
      const s = Math.min(r.w / vb[2], r.h / vb[3]);
      ctx.save();
      ctx.translate(r.x + (r.w - vb[2] * s) / 2, r.y + (r.h - vb[3] * s) / 2);
      ctx.scale(s, s);
      ctx.translate(-vb[0], -vb[1]);
      const p = new Path2D(e.d);
      const f = col(e, 'fill');
      if (f) { ctx.fillStyle = f; ctx.fill(p); }
      const st = col(e, 'stroke');
      if (st) { ctx.strokeStyle = st; ctx.lineWidth = num(e, 'stroke_width', 1) / s; ctx.lineJoin = 'round'; ctx.lineCap = 'round'; ctx.stroke(p); }
      ctx.restore();
    },

    grid(ctx, e) {
      const r = box(e);
      const cols = e.cols | 0, rows = e.rows | 0;
      if (cols <= 0 || rows <= 0) return;
      const cell = Math.min(r.w / cols, r.h / rows);
      const left = r.x + (r.w - cell * cols) / 2;
      const top = r.y + (r.h - cell * rows) / 2;
      if (e.palette && e.cells) {
        const colors = e.palette.map(parseColor);
        for (let i = 0; i < Math.min(e.cells.length, cols * rows); i++) {
          const index = e.cells[i];
          if (index < 0 || index >= colors.length || !colors[index]) continue;
          ctx.fillStyle = colors[index];
          ctx.fillRect(left + (i % cols) * cell, top + Math.floor(i / cols) * cell, cell + 0.5, cell + 0.5);
        }
      }
      const gc = col(e, 'grid_color');
      if (gc) {
        ctx.strokeStyle = gc; ctx.lineWidth = num(e, 'grid_width', 1);
        ctx.beginPath();
        for (let i = 0; i <= cols; i++) { ctx.moveTo(left + i * cell, top); ctx.lineTo(left + i * cell, top + rows * cell); }
        for (let i = 0; i <= rows; i++) { ctx.moveTo(left, top + i * cell); ctx.lineTo(left + cols * cell, top + i * cell); }
        ctx.stroke();
      }
      for (const m of e.marks || []) {
        const cx = left + (m.x + 0.5) * cell, cy = top + (m.y + 0.5) * cell;
        const half = (cell * (m.size == null ? 0.8 : m.size)) / 2;
        const mf = parseColor(m.fill), ms = parseColor(m.stroke);
        ctx.lineWidth = Math.max(1.5, cell * 0.08);
        const shape = m.shape || 'dot';
        if (shape === 'square') {
          if (mf) { ctx.fillStyle = mf; ctx.fillRect(cx - half, cy - half, half * 2, half * 2); }
          if (ms) { ctx.strokeStyle = ms; ctx.strokeRect(cx - half, cy - half, half * 2, half * 2); }
        } else if (shape === 'diamond') {
          diamond(ctx, cx, cy, half);
          if (mf) { ctx.fillStyle = mf; ctx.fill(); }
          if (ms) { ctx.strokeStyle = ms; ctx.stroke(); }
        } else if (shape === 'ring') {
          ctx.beginPath(); ctx.arc(cx, cy, half, 0, Math.PI * 2); ctx.strokeStyle = ms || mf || '#fff'; ctx.stroke();
        } else if (shape === 'cursor') {
          ctx.strokeStyle = ms || mf || '#fff'; ctx.lineWidth = Math.max(2, cell * 0.12);
          const arm = half * 0.55;
          for (const [x, y] of [[cx - half, cy - half], [cx + half, cy - half], [cx - half, cy + half], [cx + half, cy + half]]) {
            const sx = x < cx ? 1 : -1, sy = y < cy ? 1 : -1;
            ctx.beginPath(); ctx.moveTo(x + sx * arm, y); ctx.lineTo(x, y); ctx.lineTo(x, y + sy * arm); ctx.stroke();
          }
        } else {
          ctx.beginPath(); ctx.arc(cx, cy, half, 0, Math.PI * 2);
          if (mf) { ctx.fillStyle = mf; ctx.fill(); }
          if (ms) { ctx.strokeStyle = ms; ctx.stroke(); }
        }
        if (m.label) {
          ctx.font = `bold ${cell * 0.5}px sans-serif`;
          ctx.fillStyle = '#fff';
          ctx.shadowColor = '#000'; ctx.shadowBlur = cell * 0.08;
          const w = ctx.measureText(m.label).width;
          ctx.fillText(m.label, cx - w / 2, cy + cell * 0.18);
          ctx.shadowBlur = 0;
        }
      }
    },
  };

  // Loads the fonts and images a snapshot needs. `base` is the companion folder URL, or a function
  // mapping a package path to a URL (for example to inline data: URLs).
  async function loadAssets(snapshot, base) {
    const url = typeof base === 'function' ? base : (path) => base + path;
    const assets = { images: {} };
    const info = snapshot.canvas || {};
    for (const [name, file] of Object.entries(info.fonts || {})) {
      try {
        const face = new FontFace(`duo-${name}`, `url("${url(file)}")`);
        await face.load();
        document.fonts.add(face);
      } catch (err) {
        console.warn('font failed', name, file, err);
      }
    }
    const sources = new Set((snapshot.elements || []).filter((e) => e.t === 'image' && e.src).map((e) => e.src));
    await Promise.all([...sources].map((src) => new Promise((resolve) => {
      const img = new Image();
      img.onload = () => { assets.images[src] = img; resolve(); };
      img.onerror = () => resolve();
      img.src = url(src);
    })));
    return assets;
  }

  // Draws the snapshot scaled to fit a canvas of the given pixel size.
  function render(canvas, snapshot, assets) {
    const ctx = canvas.getContext('2d');
    const info = snapshot.canvas;
    const scale = Math.min(canvas.width / info.w, canvas.height / info.h);
    const dx = (canvas.width - info.w * scale) / 2;
    const dy = (canvas.height - info.h * scale) / 2;
    ctx.save();
    ctx.fillStyle = parseColor(info.background) || '#000';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.translate(dx, dy);
    ctx.scale(scale, scale);
    ctx.beginPath(); ctx.rect(0, 0, info.w, info.h); ctx.clip();
    for (const e of snapshot.elements || []) {
      const opacity = num(e, 'opacity', 1);
      if (opacity <= 0) continue;
      ctx.save();
      ctx.globalAlpha = Math.min(1, opacity);
      const draw = drawers[e.t];
      if (draw) draw(ctx, e, info, assets);
      ctx.restore();
    }
    ctx.restore();
  }

  global.DuoPreview = { render, loadAssets, parseColor };
})(typeof window !== 'undefined' ? window : globalThis);
