// The promo as a music video: colour chapters by the song's sections, the lyrics
// as the titles (big up top in the choruses, karaoke below in the verses), the
// two mascots singing along (the lead vocal's level) and dancing (the beat), the
// gadget held still while the light round it moves with the drums (beats.js), and
// cuts that mean something: into the round screen between sections, a wipe of
// pixels within one.

TITLES = false;

// ---------------------------------------------------------------- the song's sections and their colours
const SECTIONS = [
  // [from, name, top, bottom, blob tints, words: 'head' (big, up top) or 'karaoke']
  [0, 'intro', '#ffffff', '#f2f3f8', ['255,150,200', '170,140,255', '120,210,255', '140,235,200'], 'head'],
  [16.5, 'verse', '#f6faff', '#e6f0fb', ['120,190,255', '150,210,255', '190,220,255', '160,235,220'], 'karaoke'],
  [30.88, 'pre', '#faf6ff', '#ece3fb', ['190,150,255', '165,123,255', '230,170,255', '150,170,255'], 'karaoke'],
  [38.3, 'chorus', '#fff4fa', '#efe8ff', ['255,120,180', '165,110,255', '90,180,255', '255,200,120'], 'head'],
  [52.74, 'verse2', '#f6faff', '#e8f1fb', ['120,190,255', '150,210,255', '255,190,220', '160,235,220'], 'karaoke'],
  [60.58, 'chorus2', '#fff9f1', '#fdeee6', ['255,170,120', '255,130,170', '255,210,110', '140,220,170'], 'head'],
  [76.0, 'outro', '#ffffff', '#f6f1fc', ['255,150,200', '170,140,255', '120,210,255', '255,210,140'], 'head'],
];
function sectionAt(t) {
  let i = 0;
  for (let j = 0; j < SECTIONS.length; j++) if (t >= SECTIONS[j][0]) i = j;
  return i;
}
const hexRgb = h => [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16));
const mixHex = (a, b, k) => { const x = hexRgb(a), y = hexRgb(b); return `rgb(${x.map((v, i) => Math.round(lerp(v, y[i], k))).join(',')})`; };
const mixRgb = (a, b, k) => { const x = a.split(',').map(Number), y = b.split(',').map(Number); return x.map((v, i) => Math.round(lerp(v, y[i], k))).join(','); };
// The backdrop, blending into the next section's colours over the second before it.
function chapterBackdrop(ctx, t) {
  const i = sectionAt(t), cur = SECTIONS[i], nxt = SECTIONS[i + 1];
  const k = nxt ? easeInOut(seg(t, nxt[0] - 0.8, nxt[0] + 0.2)) : 0;
  const top = nxt ? mixHex(cur[2], nxt[2], k) : cur[2], bot = nxt ? mixHex(cur[3], nxt[3], k) : cur[3];
  const g = ctx.createLinearGradient(0, 0, 0, H);
  g.addColorStop(0, top);
  g.addColorStop(1, bot);
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, W, H);
  const strong = /chorus|outro/.test(cur[1]) ? 1.4 : 1;
  [[0.18, 0.3, 0.35, 560], [0.16, 0.75, 0.3, 600], [0.14, 0.6, 0.8, 640], [0.12, 0.2, 0.85, 520]].forEach(([a, bx, by, r], j) => {
    const c = nxt ? mixRgb(cur[4][j], nxt[4][j], k) : cur[4][j];
    const x = W * (bx + 0.07 * Math.sin(t * 0.19 + j * 1.7)), y = H * (by + 0.07 * Math.cos(t * 0.15 + j));
    const gg = ctx.createRadialGradient(x, y, 0, x, y, r);
    gg.addColorStop(0, `rgba(${c},${a * strong})`);
    gg.addColorStop(1, `rgba(${c},0)`);
    ctx.fillStyle = gg;
    ctx.fillRect(0, 0, W, H);
  });
}

// ---------------------------------------------------------------- the drums, round the edges
const recent = (list, t, span) => list.filter(x => x <= t && t - x < span);
function drumLights(ctx, t) {
  const sec = SECTIONS[sectionAt(t)], c = sec[4];
  const loud = /chorus|outro/.test(sec[1]) ? 1 : 0.6;
  // a kick: a soft wave of light out from behind the middle
  for (const k of recent(KICKS, t, 0.9)) {
    const q = (t - k) / 0.9;
    ctx.strokeStyle = `rgba(${c[0]},${0.28 * (1 - q) * loud})`;
    ctx.lineWidth = 40 * (1 - q) + 2;
    ctx.beginPath();
    ctx.ellipse(960, 520, 300 + q * 900, (300 + q * 900) * 0.62, 0, 0, Math.PI * 2);
    ctx.stroke();
  }
  // a snare: sparks round the edges of the frame
  for (const s of recent(SNARES, t, 0.45)) {
    const q = (t - s) / 0.45, n = Math.round(s * 100);
    for (let i = 0; i < 7; i++) {
      const side = hash(n, i) < 0.5;
      const x = side ? (hash(n, i + 10) < 0.5 ? 60 + hash(n, i + 20) * 260 : W - 60 - hash(n, i + 20) * 260) : hash(n, i + 30) * W;
      const y = side ? hash(n, i + 40) * H : (hash(n, i + 50) < 0.5 ? 40 + hash(n, i + 60) * 140 : H - 40 - hash(n, i + 60) * 140);
      const r = (6 + hash(n, i + 70) * 10) * (1 - q * 0.5);
      ctx.save();
      ctx.globalAlpha = (1 - q) * 0.8 * loud;
      ctx.translate(x, y);
      ctx.rotate(q * 2);
      ctx.fillStyle = `rgb(${c[i % 4]})`;
      ctx.beginPath();   // a four-point twinkle
      for (let p = 0; p < 8; p++) {
        const a = p * Math.PI / 4, rr = p % 2 ? r * 0.35 : r * (1 + q);
        ctx.lineTo(Math.cos(a) * rr, Math.sin(a) * rr);
      }
      ctx.fill();
      ctx.restore();
    }
  }
}

// ---------------------------------------------------------------- the two of them, singing and dancing
const vox = t => { const i = Math.floor(t * 30); return VOX[i] || 0; };
const beatIndex = () => Math.floor((GT - BEAT0) / BEAT_S);
// A dance step a beat: lean left, lean right, a hop, a wave; their mouths open with the singing.
function dance(ctx, who, x, y, h, t, o = {}) {
  const n = beatIndex() + (o.offset || 0), ph = beatPhase(), move = ((n % 4) + 4) % 4;
  const bounce = Math.sin(ph * Math.PI);
  const lean = move === 0 ? -0.13 : move === 1 ? 0.13 : 0;
  const hop = move === 2 ? bounce * h * 0.22 : bounce * h * 0.06;
  const v = o.sing === false ? 0 : vox(GT);
  ctx.save();
  ctx.translate(x, y - hop);
  ctx.rotate(lean * Math.sin(ph * Math.PI * 0.5 + 0.8));
  const p = pose(t, { squash: move === 2 ? -0.08 * bounce : 0.1 * (1 - bounce), wave: move === 3 ? 1 : 0,
    talk: v > 0.12 ? v * 0.9 : 0, happy: v <= 0.12 && move === 2, look: lean * 3 });
  if (who === 'boopie') drawBoopie(ctx, 0, 0, h, p);
  else drawMuse(ctx, 0, 0, h, p);
  ctx.restore();
}

// ---------------------------------------------------------------- the lyrics, big, up top (choruses and the intro)
function headline(ctx, t) {
  LYRICS.forEach((line, i) => {
    const next = LYRICS[i + 1];
    const out = next ? Math.min(line.t1, next.t0 - 0.12) : line.t1;
    if (t < line.t0 - 0.1 || t > out + 0.35) return;
    const size = 96, cy = 150;
    const { items } = layout(ctx, line, size);
    const leave = easeIn(seg(t, out, out + 0.3));
    items.forEach((it, j) => {
      const q = (t - it.at) / 0.3;
      if (q <= 0) return;
      const k = backOut(clamp(q));
      ctx.save();
      ctx.globalAlpha = clamp(q * 3) * (1 - leave);
      ctx.translate(960 + it.x * (1 + leave * 0.15), cy + (1 - k) * -70 - leave * 40);
      ctx.rotate((1 - clamp(q)) * (j % 2 ? 0.5 : -0.5));
      const sc = lerp(1.6, 1, k) * (1 + leave * 0.2);
      ctx.scale(sc, sc);
      ctx.font = `900 ${size}px ${SANS}`;
      ctx.textAlign = 'center';
      const kc = keyColours(it.tok) || ['#23232b', '#4a3d8f'];
      const g = ctx.createLinearGradient(-it.w / 2, -size, it.w / 2, 0);
      g.addColorStop(0, kc[0]);
      g.addColorStop(1, kc[1]);
      ctx.fillStyle = g;
      ctx.shadowColor = 'rgba(120,80,200,0.25)';
      ctx.shadowBlur = 20;
      ctx.shadowOffsetY = 8;
      ctx.fillText(it.tok, 0, size * 0.35);
      ctx.restore();
    });
  });
}
const wordsMode = t => SECTIONS[sectionAt(t)][5];

// ---------------------------------------------------------------- the intro: Hi～ 布比布比
function sIntroMV(ctx, t) {
  const k = backOut(seg(t, 1.9, 2.4));
  // a line of light drawing the ring, before the first word
  const draw = easeInOut(seg(t, 0.2, 1.9));
  if (t < 2.6) {
    ctx.save();
    ctx.globalAlpha = 1 - seg(t, 2.0, 2.6);
    const g = ctx.createConicGradient(-Math.PI / 2, 960, 470);
    g.addColorStop(0, BRAND[0]);
    g.addColorStop(0.5, BRAND[1]);
    g.addColorStop(1, BRAND[2]);
    ctx.strokeStyle = g;
    ctx.lineWidth = 6;
    ctx.lineCap = 'round';
    ctx.shadowColor = 'rgba(165,123,255,0.6)';
    ctx.shadowBlur = 30;
    ctx.beginPath();
    ctx.arc(960, 470, 230, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * draw);
    ctx.stroke();
    ctx.restore();
  }
  if (k > 0) {
    const happy = t > sungAt('Hi～布比', 1, 12);
    device(ctx, 960, 470, 200 * k, { on: seg(t, 2.0, 2.4), sweep: seg(t, 2.1, 3.2),
      screen: screenClip(happy ? 'boopie_happy' : 'boopie_idle', t) });
  }
  // they pop out on the first 布 and the third
  const pb = backOut(seg(t, sungAt('Hi～布比布比', 1), sungAt('Hi～布比布比', 1) + 0.4));
  const pm = backOut(seg(t, sungAt('布比布比', 0, 4), sungAt('布比布比', 0, 4) + 0.4));
  if (pb > 0) dance(ctx, 'boopie', 430, 930, 280 * pb, t);
  if (pm > 0) dance(ctx, 'muse', 1500, 960, 380 * pm, t, { offset: 1 });
  // the name, in the bars between
  const a = easeOut(seg(t, 9.6, 10.4));
  if (a > 0) {
    text(ctx, 'Boopie', 960, 840, { size: 150, weight: 800, gradient: BRAND, alpha: a, dy: (1 - a) * 40, ls: -4, blur: (1 - a) * 8 });
    const a2 = easeOut(seg(t, 10.2, 10.9));
    text(ctx, '你的 AI 小伙伴', 960, 915, { size: 44, weight: 700, color: '#2a2a2f', alpha: a2, ls: 6 });
    const a3 = backOut(seg(t, 10.8, 11.3));
    if (a3 > 0) pill(ctx, 'Works with Muse', 960, 985, { scale: a3, color: '#6b4fd8', size: 26 });
  }
}

// ---------------------------------------------------------------- 布比布比布比 你的 AI 小伙伴: the two of them on stage
function sStage(ctx, t, o = {}) {
  device(ctx, 960, 520, 220, { screen: screenClip(o.clip || 'boopie_happy', t), sweep: seg(t, 0.2, 1.2) });
  const pb = backOut(seg(t, 0, 0.4)), pm = backOut(seg(t, 0.15, 0.55));
  if (pb > 0) dance(ctx, 'boopie', 400, 960, 320 * pb, t);
  if (pm > 0) dance(ctx, 'muse', 1520, 990, 430 * pm, t, { offset: 2 });
  const a = easeOut(seg(t, 0.4, 0.9));
  text(ctx, 'Boopie', 960, 900, { size: 110, weight: 800, gradient: BRAND, alpha: a, ls: -3 });
}

// ---------------------------------------------------------------- 一直在你身旁: the moods, the two of them close by
function sStayMV(ctx, t) {
  sStay(ctx, t);
  dance(ctx, 'boopie', 330, 900, 230, t, { sing: true });
  dance(ctx, 'muse', 640, 920, 320, t, { offset: 1 });
}

// ---------------------------------------------------------------- the end: a wall of skins lit by the 布比s, then the name
function sOutroMV(ctx, t) {
  const T = GT;
  const chant = LYRICS.filter(l => l.t0 >= 81).flatMap(l => l.chars.map(c => c[1]));
  const n = SKINS.length, cols = 9, rows = Math.ceil(n / cols);
  const gather = easeInOut(seg(T, 88.9, 89.9));
  // the gadget and the two of them, first
  const early = 1 - seg(T, 81.0, 81.6);
  if (early > 0) {
    ctx.save();
    ctx.globalAlpha = early;
    sStage(ctx, t, { clip: 'boopie_confetti' });
    ctx.restore();
  }
  if (T > 81.0 && gather < 1) {
    for (let i = 0; i < n; i++) {
      const lit = chant[Math.min(chant.length - 1, Math.floor(i * chant.length / n))];
      const q = (T - lit) / 0.3, on = clamp(q);
      const col = i % cols, row = Math.floor(i / cols);
      const gx = 960 + (col - (cols - 1) / 2) * 190, gy = 330 + row * 135;
      const x = lerp(gx, 960, gather), y = lerp(gy, 470, gather);
      const appear = easeOut(seg(T, 81.0 + i * 0.012, 81.4 + i * 0.012));
      ctx.save();
      ctx.globalAlpha = appear * (0.25 + 0.75 * on) * (1 - gather);
      if (!on) ctx.filter = 'grayscale(1)';
      miniScreen(ctx, 'skin_' + SKINS[i], t, x, y, 58 * (on ? lerp(1.3, 1, backOut(on)) : 0.9) * (1 - gather * 0.6));
      ctx.restore();
      ctx.filter = 'none';
    }
  }
  if (gather > 0) {
    const k = Math.max(0.001, backOut(seg(T, 89.3, 89.8)));
    if (k > 0.01) device(ctx, 960, 430, 180 * k, { screen: screenClip('boopie_confetti', t), sweep: seg(T, 89.4, 90.3) });
    dance(ctx, 'boopie', 470, 880, 260 * k, t, { sing: false });
    dance(ctx, 'muse', 1450, 900, 360 * k, t, { offset: 1, sing: false });
    const a = easeOut(seg(T, 89.4, 90.0));
    text(ctx, 'Boopie', 960, 780, { size: 140, weight: 800, gradient: BRAND, alpha: a, ls: -4 });
    text(ctx, '你的 AI 小伙伴 · Works with Muse', 960, 850, { size: 36, weight: 700, color: '#2a2a2f', alpha: a, ls: 2 });
    text(ctx, '概念演示 · 画面为模拟渲染', 1880, 1050, { size: 18, weight: 500, color: '#a1a1a6', align: 'right', alpha: a });
  }
}

// ---------------------------------------------------------------- the timeline
// [start, end, scene, natural length, cut in: 'portal' (into the round screen) or 'pixels'].
const TIMELINE = [
  [0, 16.5, sIntroMV, 16.5],
  [16.5, 20.14, sProductSong, 3.8, 'portal'],
  [20.14, 23.64, sMorning, 3.6, 'pixels'],
  [23.64, 27.22, sSetup, 3.6, 'pixels'],
  [27.22, 30.88, sConnect, 4.6, 'pixels'],
  [30.88, 34.68, sMuse, 7.0, 'portal'],
  [34.68, 38.3, sRemote, 7.2, 'pixels'],
  [38.3, 41.88, sStage, 3.58, 'portal'],
  [41.88, 45.5, sVoice, 5.2, 'pixels'],
  [45.5, 49.24, sCommand, 4.0, 'pixels'],
  [49.24, 52.74, sCosmos, 3.3, 'pixels'],
  [52.74, 56.8, sSkins, 6.4, 'portal'],
  [56.8, 60.58, sGames, 5.0, 'pixels'],
  [60.58, 71.3, sWorldSong, 10.8, 'portal'],
  [71.3, 76.0, sStayMV, 5.0, 'pixels'],
  [76.0, 90.9, sOutroMV, 14.9, 'portal'],
];
const DURATION = 90.6;
const CUT = 0.5;   // how long a cut takes

// The mask a scene cuts in through, p 0..1: a circle out of the gadget, or squares in a sweep.
function cutMask(c, kind, p) {
  c.beginPath();
  if (kind === 'portal') {
    c.arc(960, 520, easeInOut(p) * 1250 + 1, 0, Math.PI * 2);
  } else {
    const S = 80;
    for (let y = 0; y < H; y += S) {
      for (let x = 0; x < W; x += S) {
        const at = (x / W) * 0.65 + hash(x, y) * 0.35;
        const q = clamp((p * 1.35 - at) / 0.35);
        if (q > 0) { const s = S * easeOut(q); c.rect(x + (S - s) / 2, y + (S - s) / 2, s + 0.5, s + 0.5); }
      }
    }
  }
}
function cutRim(c, kind, p) {
  if (kind !== 'portal' || p >= 1) return;
  const r = easeInOut(p) * 1250;
  const g = c.createLinearGradient(960 - r, 0, 960 + r, 0);
  g.addColorStop(0, BRAND[0]);
  g.addColorStop(0.5, BRAND[1]);
  g.addColorStop(1, BRAND[2]);
  c.strokeStyle = g;
  c.lineWidth = 18 * (1 - p) + 2;
  c.globalAlpha = 1 - p;
  c.beginPath();
  c.arc(960, 520, r, 0, Math.PI * 2);
  c.stroke();
  c.globalAlpha = 1;
}
