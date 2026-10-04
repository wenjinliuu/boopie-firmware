// Drawing helpers for the promo video: timing, the light backdrop, the round
// device, screen clips from the simulator, type and cards.

const W = 1920, H = 1080, FPS = 30;
const clamp = (v, a = 0, b = 1) => Math.max(a, Math.min(b, v));
const lerp = (a, b, k) => a + (b - a) * k;
const seg = (t, a, b) => clamp((t - a) / (b - a));
const easeOut = k => 1 - Math.pow(1 - k, 3);
const easeIn = k => k * k * k;
const easeInOut = k => (k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2);
const backOut = k => { const c = 1.7; return 1 + (c + 1) * Math.pow(k - 1, 3) + c * Math.pow(k - 1, 2); };
const elastic = k => (k === 0 || k === 1 ? k : Math.pow(2, -10 * k) * Math.sin((k * 10 - 0.75) * (2 * Math.PI) / 3) + 1);
const hash = (i, k = 0) => { const x = Math.sin(i * 12.9898 + k * 78.233) * 43758.5453; return x - Math.floor(x); };

// ---------------------------------------------------------------- clips from the simulator
// CLIPS (clips.js): name -> frame count; frames are 15 a second.
const cache = new Map();
let pending = [];
function img(src) {
  let e = cache.get(src);
  if (!e) {
    e = new Image();
    e.src = src;
    cache.set(src, e);
    if (cache.size > 900) {           // the oldest out
      const k = cache.keys().next().value;
      cache.delete(k);
    }
  }
  if (!e.complete) pending.push(e.decode().catch(() => {}));
  return e;
}
function clipFrame(name, t, loop = true) {
  const n = CLIPS[name] || 1;
  let i = Math.floor(Math.max(0, t) * 15);
  i = loop ? i % n : Math.min(i, n - 1);
  return img(`clips/${name}/${String(i).padStart(4, '0')}.png`);
}
// A clip in a circle of radius r (the round screen), or a rounded square when square is set.
function drawClip(ctx, name, t, cx, cy, r, opts = {}) {
  const im = clipFrame(name, t, opts.loop !== false);
  ctx.save();
  ctx.beginPath();
  if (opts.square) roundRect(ctx, cx - r, cy - r, r * 2, r * 2, opts.square);
  else ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.clip();
  ctx.imageSmoothingEnabled = false;
  const z = opts.zoom || 1;
  if (im.complete && im.naturalWidth) ctx.drawImage(im, cx - r * z, cy - r * z, r * 2 * z, r * 2 * z);
  else { ctx.fillStyle = '#000'; ctx.fill(); }
  ctx.restore();
}

// ---------------------------------------------------------------- shapes and type
function roundRect(ctx, x, y, w, h, r) {
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}
function card(ctx, x, y, w, h, r = 28, opts = {}) {
  ctx.save();
  ctx.shadowColor = opts.shadow || 'rgba(40,40,80,0.10)';
  ctx.shadowBlur = opts.blur ?? 40;
  ctx.shadowOffsetY = opts.dy ?? 14;
  ctx.fillStyle = opts.fill || 'rgba(255,255,255,0.92)';
  ctx.beginPath();
  roundRect(ctx, x, y, w, h, r);
  ctx.fill();
  ctx.restore();
  if (opts.stroke) {
    ctx.save();
    ctx.strokeStyle = opts.stroke;
    ctx.lineWidth = opts.lw || 3;
    ctx.beginPath();
    roundRect(ctx, x, y, w, h, r);
    ctx.stroke();
    ctx.restore();
  }
}
const SANS = '"Inter", "Noto Sans SC", sans-serif';
const BRAND = ['#ff6fa5', '#a57bff', '#4fb6ff'];
function text(ctx, s, x, y, o = {}) {
  ctx.save();
  ctx.globalAlpha *= o.alpha ?? 1;
  ctx.font = `${o.weight || 700} ${o.size || 48}px ${SANS}`;
  ctx.textAlign = o.align || 'center';
  ctx.textBaseline = o.base || 'alphabetic';
  if ('ls' in o) ctx.letterSpacing = `${o.ls}px`;
  if (o.gradient) {
    const m = ctx.measureText(s), w = m.width;
    const x0 = o.align === 'left' ? x : o.align === 'right' ? x - w : x - w / 2;
    const g = ctx.createLinearGradient(x0, 0, x0 + w, 0);
    o.gradient.forEach((c, i) => g.addColorStop(i / (o.gradient.length - 1), c));
    ctx.fillStyle = g;
  } else ctx.fillStyle = o.color || '#1d1d1f';
  if (o.blur) ctx.filter = `blur(${o.blur}px)`;
  ctx.fillText(s, x, y + (o.dy || 0));
  ctx.restore();
}
// A title that rises in and blurs out: (t local, in at a, out at b).
function title(ctx, main, sub, x, y, t, a, b, o = {}) {
  const k = easeOut(seg(t, a, a + 0.6)), q = seg(t, b - 0.4, b);
  const al = k * (1 - q);
  if (al <= 0) return;
  text(ctx, main, x, y, { size: o.size || 84, weight: o.weight || 800, alpha: al, dy: (1 - k) * 40 - q * 20,
    blur: (1 - k) * 8 + q * 8, gradient: o.gradient, align: o.align, ls: o.ls ?? -1, color: o.color });
  if (sub) {
    const k2 = easeOut(seg(t, a + 0.15, a + 0.8));
    text(ctx, sub, x, y + (o.gap || 62), { size: o.subSize || 32, weight: 500, color: '#6e6e73',
      alpha: k2 * (1 - q), dy: (1 - k2) * 30, align: o.align, blur: q * 6 });
  }
}
function pill(ctx, s, cx, cy, o = {}) {
  ctx.save();
  ctx.globalAlpha *= o.alpha ?? 1;
  ctx.font = `${o.weight || 600} ${o.size || 26}px ${SANS}`;
  const w = ctx.measureText(s).width + (o.pad || 44), h = (o.size || 26) * 1.9;
  ctx.translate(cx, cy);
  if (o.scale) ctx.scale(o.scale, o.scale);
  card(ctx, -w / 2, -h / 2, w, h, h / 2, { fill: o.fill || 'rgba(255,255,255,0.95)', blur: 24, dy: 8 });
  ctx.fillStyle = o.color || '#1d1d1f';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillText(s, 0, 2);
  ctx.restore();
  return w;
}

// ---------------------------------------------------------------- the backdrop
function backdrop(ctx, t) {
  const g = ctx.createLinearGradient(0, 0, 0, H);
  g.addColorStop(0, '#fbfbfd');
  g.addColorStop(1, '#eef0f6');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, W, H);
  const blobs = [
    ['255,150,200', 0.18, 0.3, 0.35, 520],
    ['170,140,255', 0.16, 0.75, 0.3, 560],
    ['120,210,255', 0.14, 0.6, 0.8, 600],
    ['140,235,200', 0.10, 0.2, 0.85, 480],
  ];
  blobs.forEach(([c, a, bx, by, r], i) => {
    const x = W * (bx + 0.06 * Math.sin(t * 0.21 + i * 1.7)), y = H * (by + 0.06 * Math.cos(t * 0.17 + i));
    const gg = ctx.createRadialGradient(x, y, 0, x, y, r);
    gg.addColorStop(0, `rgba(${c},${a})`);
    gg.addColorStop(1, `rgba(${c},0)`);
    ctx.fillStyle = gg;
    ctx.fillRect(0, 0, W, H);
  });
}
function sparkles(ctx, t, n = 30, alpha = 1, seed = 0) {
  for (let i = 0; i < n; i++) {
    const x = hash(i, seed) * W, y = (hash(i, seed + 1) * H - t * (12 + hash(i, 3) * 20)) % H;
    const yy = y < 0 ? y + H : y, tw = 0.5 + 0.5 * Math.sin(t * 3 + i);
    ctx.fillStyle = `rgba(160,140,255,${0.35 * tw * alpha})`;
    ctx.beginPath();
    ctx.arc(x, yy, 2 + hash(i, 5) * 3, 0, Math.PI * 2);
    ctx.fill();
  }
}

// ---------------------------------------------------------------- the device
// A round gadget: a brushed silver ring, a black glass bezel, the screen drawn by
// `screen(ctx, r)` with the origin at its centre. rot turns it about the
// vertical axis (radians); tilt leans it.
function device(ctx, cx, cy, R, o = {}) {
  const rot = o.rot || 0, sx = Math.cos(rot), side = Math.sin(rot);
  const ring = R * 1.16, bez = R * 1.045;
  ctx.save();
  ctx.translate(cx, cy);
  if (o.tilt) ctx.rotate(o.tilt);
  if (o.scale) ctx.scale(o.scale, o.scale);
  // the shadow on the floor
  const sh = ctx.createRadialGradient(0, ring * 1.05, 0, 0, ring * 1.05, ring * 1.1);
  sh.addColorStop(0, 'rgba(40,40,70,0.22)');
  sh.addColorStop(1, 'rgba(40,40,70,0)');
  ctx.save();
  ctx.scale(1, 0.16);
  ctx.fillStyle = sh;
  ctx.beginPath();
  ctx.arc(0, ring * 1.05 / 0.16, ring * 1.1, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
  // its edge, seen as it turns
  if (Math.abs(side) > 0.01) {
    const depth = R * 0.42 * side;
    const eg = ctx.createLinearGradient(-ring, 0, ring, 0);
    eg.addColorStop(0, '#9da1aa');
    eg.addColorStop(0.5, '#e9ebef');
    eg.addColorStop(1, '#8c9099');
    ctx.fillStyle = eg;
    const steps = 14;
    for (let i = steps; i >= 0; i--) {
      ctx.beginPath();
      ctx.ellipse(depth * i / steps, 0, ring * Math.abs(sx), ring, 0, 0, Math.PI * 2);
      ctx.fill();
    }
    // the buttons on its side
    ctx.fillStyle = '#c9ccd3';
    for (const by of [-0.28, 0.32]) {
      ctx.beginPath();
      ctx.ellipse(depth + ring * Math.abs(sx) * Math.sign(side) * 0.02, by * ring, Math.abs(depth) * 0.25 + 2, R * 0.09, 0, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  ctx.scale(sx, 1);
  // the ring: brushed metal
  const cg = ctx.createConicGradient(-0.6 + rot, 0, 0);
  [['#f4f5f7', 0], ['#b9bdc6', 0.12], ['#fdfdfe', 0.25], ['#a7abb5', 0.4], ['#e8eaee', 0.55], ['#9ea2ac', 0.7],
   ['#f6f7f9', 0.85], ['#f4f5f7', 1]].forEach(([c, k]) => cg.addColorStop(k, c));
  ctx.shadowColor = 'rgba(30,30,60,0.25)';
  ctx.shadowBlur = R * 0.25;
  ctx.shadowOffsetY = R * 0.08;
  ctx.fillStyle = cg;
  ctx.beginPath();
  ctx.arc(0, 0, ring, 0, Math.PI * 2);
  ctx.fill();
  ctx.shadowColor = 'transparent';
  const rim = ctx.createLinearGradient(0, -ring, 0, ring);
  rim.addColorStop(0, 'rgba(255,255,255,0.9)');
  rim.addColorStop(0.5, 'rgba(255,255,255,0)');
  rim.addColorStop(1, 'rgba(0,0,0,0.12)');
  ctx.strokeStyle = rim;
  ctx.lineWidth = R * 0.02;
  ctx.beginPath();
  ctx.arc(0, 0, ring - R * 0.01, 0, Math.PI * 2);
  ctx.stroke();
  // the bezel
  ctx.fillStyle = '#0b0b0f';
  ctx.beginPath();
  ctx.arc(0, 0, bez, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = 'rgba(0,0,0,0.35)';
  ctx.lineWidth = R * 0.012;
  ctx.stroke();
  // the screen
  ctx.save();
  ctx.beginPath();
  ctx.arc(0, 0, R, 0, Math.PI * 2);
  ctx.clip();
  ctx.fillStyle = '#000';
  ctx.fillRect(-R, -R, R * 2, R * 2);
  if (o.screen && (o.on ?? 1) > 0) {
    ctx.globalAlpha = o.on ?? 1;
    o.screen(ctx, R);
    ctx.globalAlpha = 1;
  }
  ctx.restore();
  // the glass: a soft sheen across the top left
  const gl = ctx.createLinearGradient(-bez, -bez, bez * 0.4, bez * 0.4);
  gl.addColorStop(0, 'rgba(255,255,255,0.22)');
  gl.addColorStop(0.45, 'rgba(255,255,255,0.04)');
  gl.addColorStop(0.46, 'rgba(255,255,255,0)');
  ctx.fillStyle = gl;
  ctx.beginPath();
  ctx.arc(0, 0, bez, 0, Math.PI * 2);
  ctx.fill();
  if (o.sweep !== undefined) {         // a light sweeping over the glass
    const k = o.sweep, sx0 = lerp(-bez * 2, bez * 2, k);
    ctx.save();
    ctx.beginPath();
    ctx.arc(0, 0, bez, 0, Math.PI * 2);
    ctx.clip();
    const sg = ctx.createLinearGradient(sx0 - bez * 0.3, 0, sx0 + bez * 0.3, 0);
    sg.addColorStop(0, 'rgba(255,255,255,0)');
    sg.addColorStop(0.5, 'rgba(255,255,255,0.28)');
    sg.addColorStop(1, 'rgba(255,255,255,0)');
    ctx.rotate(-0.5);
    ctx.fillStyle = sg;
    ctx.fillRect(-bez * 3, -bez * 3, bez * 6, bez * 6);
    ctx.restore();
  }
  ctx.restore();
}
// Just the screen's clip: the simulator's 466 x 466 frame filling the circle.
const screenClip = (name, t, o = {}) => (ctx, r) => drawClip(ctx, name, t, 0, 0, r, o);

// A small round screen, as a skin card.
function miniScreen(ctx, name, t, cx, cy, r, o = {}) {
  ctx.save();
  ctx.globalAlpha *= o.alpha ?? 1;
  ctx.shadowColor = 'rgba(40,40,80,0.18)';
  ctx.shadowBlur = r * 0.4;
  ctx.shadowOffsetY = r * 0.12;
  ctx.fillStyle = '#e6e8ee';
  ctx.beginPath();
  ctx.arc(cx, cy, r * 1.1, 0, Math.PI * 2);
  ctx.fill();
  ctx.shadowColor = 'transparent';
  ctx.fillStyle = '#0b0b0f';
  ctx.beginPath();
  ctx.arc(cx, cy, r * 1.03, 0, Math.PI * 2);
  ctx.fill();
  drawClip(ctx, name, t, cx, cy, r, { zoom: o.zoom || 1 });
  ctx.restore();
}

// A phone, for scanning: a rounded slab with a camera view.
function phone(ctx, cx, cy, h, draw) {
  const w = h * 0.48, r = h * 0.09;
  ctx.save();
  ctx.translate(cx, cy);
  card(ctx, -w / 2, -h / 2, w, h, r, { fill: '#1b1b20', blur: 60, dy: 24, shadow: 'rgba(20,20,50,0.25)' });
  ctx.fillStyle = '#f7f8fb';
  ctx.beginPath();
  roundRect(ctx, -w / 2 + h * 0.018, -h / 2 + h * 0.018, w - h * 0.036, h - h * 0.036, r * 0.82);
  ctx.fill();
  ctx.save();
  ctx.clip();
  draw(ctx, w - h * 0.036, h - h * 0.036);
  ctx.restore();
  ctx.fillStyle = '#1b1b20';
  ctx.beginPath();
  roundRect(ctx, -w * 0.16, -h / 2 + h * 0.03, w * 0.32, h * 0.035, h * 0.018);
  ctx.fill();
  ctx.restore();
}
