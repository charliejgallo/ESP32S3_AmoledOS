/*
 * AmoledOS - el intérprete de iconos AIC del portal, para /iconos y /menu.
 *
 * Estaba adentro de iconos.html; /menu tambien dibuja los iconos de las apps
 * y dos copias del mismo interprete terminan distintas. Los comentarios de
 * abajo vienen de alla, en ingles como el resto del codigo que describen.
 */
(function () {
"use strict";

/* --------------------------------------------------------------------------
 * The AIC interpreter, for the browser. Same opcodes as walk_ops() in
 * components/aos_ui/aos_icon.c: percent coordinates, the palette, INTO/OUT.
 * Not pixel-identical to LVGL (fonts, anti-aliasing) but the same shapes in
 * the same places, which is what a preview is for.
 * ------------------------------------------------------------------------- */
const PALETTE = ["FFFFFF","000000","1C1C1E","2C2C2E","8E8E93","0A84FF","30D158",
                 "FF453A","FF9F0A","FFD60A","BF5AF2","FF375F","40C8E0"];

function parse(b) {
  if (b.length < 4 || b.length > 256 || b[0] !== 65 || b[1] !== 73 || b[2] !== 67 || b[3] !== 1) return null;
  let at = 4; const ops = [];
  const u8 = () => b[at++];
  const i8 = () => { const v = b[at++]; return v > 127 ? v - 256 : v; };
  const i16 = () => { const v = b[at] | (b[at + 1] << 8); at += 2; return v > 32767 ? v - 65536 : v; };
  const col = () => { const i = u8(); if (i === 0xFF) { const r = u8(), g = u8(), bb = u8();
                      return "#" + [r, g, bb].map(x => x.toString(16).padStart(2, "0")).join(""); }
                      return i < PALETTE.length ? "#" + PALETTE[i] : "#ff00ff"; };
  while (at < b.length) {
    const op = u8();
    switch (op) {
      case 0: return ops;
      case 1: ops.push({ op: "rect", align: u8(), x: i8(), y: i8(), w: i8(), h: i8(), r: u8(), c: col(), opa: u8() }); break;
      case 2: ops.push({ op: "ring", d: i8(), bw: i8(), c: col(), opa: u8() }); break;
      case 3: ops.push({ op: "arc", align: u8(), x: i8(), y: i8(), d: i8(), wt: i8(), wi: i8(), bs: i16(), be: i16(), is: i16(), ie: i16(),
                         rot: i16(), ct: col(), ot: u8(), ci: col(), oi: u8() }); break;
      case 4: ops.push({ op: "hand", w: i8(), l: i8(), a: i16(), c: col() }); break;
      case 5: { const f = u8(), n = u8(); const s = new TextDecoder().decode(b.slice(at, at + n)); at += n;
                ops.push({ op: "text", font: f, s: s }); break; }
      case 6: ops.push({ op: "rot", a: i16() }); break;
      case 7: ops.push({ op: "border", w: i8(), c: col(), opa: u8() }); break;
      case 8: ops.push({ op: "grad", c: col(), dir: u8() }); break;
      case 9: ops.push({ op: "into" }); break;
      case 10: ops.push({ op: "out" }); break;
      default: return ops;
    }
    if (at > b.length) return ops;
  }
  return ops;
}

function rgba(hex, opa) {
  const n = parseInt(hex.slice(1), 16);
  return "rgba(" + (n >> 16 & 255) + "," + (n >> 8 & 255) + "," + (n & 255) + "," + (opa / 255) + ")";
}

function roundRect(ctx, x, y, w, h, r) {
  r = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}

/* One drawn shape: its box in canvas px, so that INTO can hang children off it. */
function place(parent, align, x, y, w, h) {
  let px, py;
  const cx = parent.x + parent.w / 2 - w / 2, cy = parent.y + parent.h / 2 - h / 2;
  switch (align) {
    case 1: px = parent.x; py = parent.y; break;                                   /* TOP_LEFT     */
    case 2: px = cx; py = parent.y; break;                                         /* TOP_MID      */
    case 3: px = parent.x + parent.w - w; py = parent.y; break;                    /* TOP_RIGHT    */
    case 4: px = parent.x; py = parent.y + parent.h - h; break;                    /* BOTTOM_LEFT  */
    case 5: px = cx; py = parent.y + parent.h - h; break;                          /* BOTTOM_MID   */
    case 6: px = parent.x + parent.w - w; py = parent.y + parent.h - h; break;     /* BOTTOM_RIGHT */
    case 7: px = parent.x; py = cy; break;                                         /* LEFT_MID     */
    case 8: px = parent.x + parent.w - w; py = cy; break;                          /* RIGHT_MID    */
    default: px = cx; py = cy; break;                                              /* CENTER       */
  }
  return { x: px + x, y: py + y, w: w, h: h };
}

function render(canvas, app, bytes, size) {
  const S = size || 82, dpr = window.devicePixelRatio || 1;
  canvas.width = S * dpr; canvas.height = S * dpr;
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);

  /* the base: a circle with the app's vertical gradient */
  const g = ctx.createLinearGradient(0, 0, 0, S);
  g.addColorStop(0, "#" + app.color_a); g.addColorStop(1, "#" + app.color_b);
  ctx.fillStyle = g;
  ctx.beginPath(); ctx.arc(S / 2, S / 2, S / 2, 0, Math.PI * 2); ctx.fill();

  const ops = bytes ? parse(bytes) : null;
  if (!ops) {
    /* no bytes: a firmware case or a glyph. Say so instead of inventing. */
    ctx.strokeStyle = "rgba(255,255,255,.45)"; ctx.setLineDash([3, 4]); ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(S / 2, S / 2, S * 0.3, 0, Math.PI * 2); ctx.stroke();
    return;
  }

  /* offsets are percent; a negative dimension is size / n (AIC_DIV) */
  const pct = v => Math.trunc(S * v / 100);
  const dim = v => { if (!v) return 0; const p = v > 0 ? Math.trunc(S * v / 100) : Math.trunc(S / -v); return Math.max(1, p); };
  const rad = v => v === 0xFF ? 1e9 : (v > 127 ? dim(v - 256) : dim(v));
  const root = { x: 0, y: 0, w: S, h: S };
  const stack = []; let parent = root; let last = null;

  const draw = shape => {
    ctx.save();
    if (shape.rot) {
      ctx.translate(shape.x + shape.w / 2, shape.y + shape.h / 2);
      ctx.rotate(shape.rot / 10 * Math.PI / 180);
      ctx.translate(-(shape.x + shape.w / 2), -(shape.y + shape.h / 2));
    }
    if (shape.kind === "rect") {
      const r = Math.min(rad(shape.r), Math.min(shape.w, shape.h) / 2);
      if (shape.grad) {
        const gg = shape.grad.dir === 2 ? ctx.createLinearGradient(shape.x, 0, shape.x + shape.w, 0)
                                        : ctx.createLinearGradient(0, shape.y, 0, shape.y + shape.h);
        gg.addColorStop(0, rgba(shape.c, shape.opa)); gg.addColorStop(1, rgba(shape.grad.c, shape.opa));
        ctx.fillStyle = gg;
      } else {
        ctx.fillStyle = rgba(shape.c, shape.opa);
      }
      roundRect(ctx, shape.x, shape.y, shape.w, shape.h, r);
      if (shape.opa) ctx.fill();
      if (shape.border) {
        const bw = shape.border.w;
        ctx.strokeStyle = rgba(shape.border.c, shape.border.opa); ctx.lineWidth = bw;
        roundRect(ctx, shape.x + bw / 2, shape.y + bw / 2, shape.w - bw, shape.h - bw, Math.max(0, r - bw / 2));
        ctx.stroke();
      }
    } else if (shape.kind === "arc") {
      const cx = shape.x + shape.w / 2, cy = shape.y + shape.h / 2, deg = Math.PI / 180;
      ctx.lineCap = "round";
      if (shape.wt && shape.ot) {
        ctx.lineWidth = shape.wt; ctx.strokeStyle = rgba(shape.ct, shape.ot);
        ctx.beginPath(); ctx.arc(cx, cy, shape.w / 2 - shape.wt / 2, (shape.rot + shape.bs) * deg, (shape.rot + shape.be) * deg); ctx.stroke();
      }
      if (shape.wi && shape.oi && shape.ie !== shape.is) {
        ctx.lineWidth = shape.wi; ctx.strokeStyle = rgba(shape.ci, shape.oi);
        ctx.beginPath(); ctx.arc(cx, cy, shape.w / 2 - shape.wi / 2, (shape.rot + shape.is) * deg, (shape.rot + shape.ie) * deg); ctx.stroke();
      }
    } else if (shape.kind === "hand") {
      /* pivot at the parent's centre, 0 = twelve o'clock, clockwise */
      const cx = shape.px, cy = shape.py;
      ctx.translate(cx, cy); ctx.rotate(shape.a / 10 * Math.PI / 180);
      ctx.fillStyle = shape.c;
      roundRect(ctx, -shape.w / 2, -shape.l, shape.w, shape.l, shape.w / 2); ctx.fill();
    } else if (shape.kind === "text") {
      ctx.fillStyle = "#fff"; ctx.textAlign = "center"; ctx.textBaseline = "middle";
      ctx.font = (shape.font === 1 ? 28 : 20) + "px system-ui, sans-serif";
      ctx.fillText(shape.s, shape.x + shape.w / 2, shape.y + shape.h / 2);
    }
    ctx.restore();
  };

  /* Two passes are not needed: LVGL draws in creation order and so do we,
     but ROT/BORDER/GRAD modify the LAST shape, which is already drawn. So
     shapes are collected and each is drawn when the next one starts, or at
     the end - by then its modifiers have arrived. */
  let pending = null;
  const flush = () => { if (pending) { draw(pending); pending = null; } };

  for (const o of ops) {
    switch (o.op) {
      case "rect": {
        flush();
        const b = place(parent, o.align, pct(o.x), pct(o.y), dim(o.w), dim(o.h));
        pending = Object.assign({ kind: "rect", r: o.r, c: o.c, opa: o.opa }, b);
        last = pending; break;
      }
      case "ring": {
        flush();
        const d = dim(o.d), b = place(parent, 9, 0, 0, d, d);
        pending = Object.assign({ kind: "rect", r: 0xFF, c: o.c, opa: 0,
                                  border: { w: dim(o.bw), c: o.c, opa: o.opa } }, b);
        last = pending; break;
      }
      case "arc": {
        flush();
        const d = dim(o.d), b = place(parent, o.align, pct(o.x), pct(o.y), d, d);
        pending = Object.assign({ kind: "arc", wt: dim(o.wt), wi: dim(o.wi), bs: o.bs, be: o.be, is: o.is, ie: o.ie,
                                  rot: o.rot, ct: o.ct, ot: o.ot, ci: o.ci, oi: o.oi }, b);
        last = pending; break;
      }
      case "hand": {
        flush();
        pending = { kind: "hand", w: dim(o.w), l: dim(o.l), a: o.a, c: o.c,
                    px: parent.x + parent.w / 2, py: parent.y + parent.h / 2,
                    x: parent.x, y: parent.y, w: parent.w, h: parent.h };
        last = pending; break;
      }
      case "text": {
        flush();
        pending = Object.assign({ kind: "text", s: o.s, font: o.font }, parent);
        last = pending; break;
      }
      case "rot":    if (pending) pending.rot = o.a; break;
      case "border": if (pending && pending.kind === "rect") pending.border = { w: dim(o.w), c: o.c, opa: o.opa }; break;
      case "grad":   if (pending && pending.kind === "rect") pending.grad = { c: o.c, dir: o.dir }; break;
      case "into":   flush(); if (last) { stack.push(parent); parent = last; } break;
      case "out":    flush(); if (stack.length) parent = stack.pop(); break;
    }
  }
  flush();
}


window.AIC = { parse: parse, render: render };
})();
