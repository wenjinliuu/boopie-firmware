// The promo cut to the song (song/boopie.mp3): its karaoke, the scenes made for
// particular lines, and the timeline, each scene starting on the line it shows.
// LYRICS (lyrics.js): lines, each character with the second it's sung.

// ---------------------------------------------------------------- the karaoke
const LATIN = /^[A-Za-z]/;
const KEY_COLOURS = [
  [/^[布比]$/, ['#ff6fa5', '#c06bff']],
  [/^Muse$/i, ['#a57bff', '#6b4fd8']],
  [/^(AI|Wi-Fi|works|with)$/i, ['#4fb6ff', '#6b8cff']],
  [/^[蓝牙心宇宙世界]$/, ['#4fb6ff', '#a57bff']],
];
function keyColours(tok) {
  for (const [re, c] of KEY_COLOURS) if (re.test(tok)) return c;
  return null;
}
// Lays a line out: each token with its x (centre) and width, centred on cx.
function layout(ctx, line, size) {
  ctx.font = `800 ${size}px ${SANS}`;
  const items = [];
  let x = 0;
  line.chars.forEach(([tok, at], i) => {
    const prev = line.chars[i - 1];
    if (prev && (LATIN.test(tok) || LATIN.test(prev[0]))) x += size * 0.28;   // a space round Latin words
    const w = ctx.measureText(tok).width;
    items.push({ tok, at, x: x + w / 2, w });
    x += w + size * 0.04;
  });
  const total = x;
  items.forEach(it => { it.x -= total / 2; });
  return { items, total };
}
function karaoke(ctx, t) {
  // the line leaving flies off while the next comes in
  LYRICS.forEach((line, i) => {
    const next = LYRICS[i + 1];
    // out just before the next one comes in: they never share the strip
    const out = next ? Math.min(line.t1, next.t0 - 0.22) : line.t1;
    if (t >= line.t0 - 0.2 && t < out + 0.4) karaokeLine(ctx, t, { ...line, t1: out });
  });
}
function karaokeLine(ctx, t, line) {
  const size = 64, cx = 960, cy = 1028;
  const { items, total } = layout(ctx, line, size);
  const enter = easeOut(seg(t, line.t0 - 0.2, line.t0 + 0.02)), leave = seg(t, line.t1, line.t1 + 0.1);
  // a soft glass strip behind
  ctx.save();
  ctx.globalAlpha = enter * (1 - leave);
  const bw = total + 120, bh = 104;
  card(ctx, cx - bw / 2, cy - bh / 2 - 6, bw, bh, bh / 2, { fill: 'rgba(255,255,255,0.72)', blur: 30, dy: 10 });
  ctx.restore();
  const muse = line.chars.some(([c]) => /Muse|电|脑/.test(c));
  let hopX = items[0].x - items[0].w, hopY = 0;
  items.forEach((it, i) => {
    const sung = t >= it.at;
    const pop = sung ? clamp((t - it.at) / 0.22) : 0;
    const k = backOut(pop);
    // in, a character at a time; out, flying up and away
    const inK = easeOut(seg(t, line.t0 - 0.2 + i * 0.012, line.t0 + 0.02 + i * 0.012));
    const outK = easeOut(seg(t, line.t1 + i * 0.006, line.t1 + 0.16 + i * 0.006));
    if (inK <= 0 || outK >= 1) return;
    const x = cx + it.x, y = cy + 22 + (1 - inK) * 40 - outK * 130 - (sung ? Math.sin(pop * Math.PI) * 18 : 0);
    const sc = sung ? lerp(1.35, 1, k) : 0.92;
    ctx.save();
    ctx.globalAlpha = inK * (1 - outK) * (1 - outK);
    ctx.translate(x, y);
    ctx.rotate(sung ? Math.sin(pop * Math.PI) * (i % 2 ? 0.12 : -0.12) : 0);
    ctx.scale(sc, sc);
    ctx.font = `800 ${size}px ${SANS}`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'alphabetic';
    if (sung) {
      const kc = keyColours(it.tok) || ['#2a2a35', '#4b3a8a'];
      const g = ctx.createLinearGradient(-it.w / 2, -size, it.w / 2, 0);
      g.addColorStop(0, kc[0]);
      g.addColorStop(1, kc[1]);
      ctx.fillStyle = g;
      ctx.shadowColor = keyColours(it.tok) ? 'rgba(200,100,255,0.45)' : 'rgba(80,60,160,0.2)';
      ctx.shadowBlur = 18 * (1 - pop) + 6;
    } else {
      ctx.fillStyle = 'rgba(60,60,80,0.22)';
    }
    ctx.fillText(it.tok, 0, 0);
    ctx.restore();
    // a few sparks as a key character lands
    if (sung && pop < 1 && keyColours(it.tok)) {
      for (let s = 0; s < 6; s++) {
        const a = s / 6 * Math.PI * 2 + i, d = 20 + pop * 46;
        ctx.fillStyle = `rgba(255,${150 + s * 15},${200 - s * 10},${1 - pop})`;
        ctx.beginPath();
        ctx.arc(x + Math.cos(a) * d, y - size * 0.35 + Math.sin(a) * d, 4 * (1 - pop) + 1, 0, Math.PI * 2);
        ctx.fill();
      }
    }
  });
  // the little one hopping from character to character as they're sung
  for (let i = 0; i < items.length; i++) {
    const a = items[i], b = items[i + 1];
    if (t < a.at) {
      if (i === 0) { hopX = a.x - a.w * 0.9; hopY = 0; }
      break;
    }
    if (!b || t < b.at) {
      const span = b ? Math.min(b.at - a.at, 0.6) : 0.3, k = clamp((t - a.at) / span);
      hopX = b ? lerp(a.x, b.x, easeInOut(k)) : a.x;
      hopY = Math.sin(Math.min(1, (t - a.at) / span) * Math.PI) * 30;
      break;
    }
  }
  const hk = enter * (1 - leave);
  if (hk > 0) {
    ctx.save();
    ctx.globalAlpha = hk;
    const hx = cx + hopX, hy = cy - size * 0.78 - hopY;
    if (muse) drawMuse(ctx, hx, hy, 58, pose(t, { bow: false, squash: hopY < 3 ? 0.15 : -0.05 }));
    else drawBoopie(ctx, hx, hy, 44, pose(t, { squash: hopY < 3 ? 0.18 : -0.06, happy: true }));
    ctx.restore();
  }
}
// The time a line's n-th character is sung (the first line whose text starts with `start` after `after`).
function sungAt(start, n, after = 0) {
  const l = LYRICS.find(l => l.t0 >= after && l.chars.map(c => c[0]).join('').startsWith(start));
  return l ? l.chars[Math.min(n, l.chars.length - 1)][1] : 0;
}

// ---------------------------------------------------------------- 布比布比: the chant, the big characters on the beat
// Shows the current line's characters huge, each landing as it's sung, the two of them bouncing.
function sChant(ctx, t, o = {}) {
  const kick = beatKick();
  for (let i = 0; i < 4; i++) {
    const p = ((GT - BEAT0) / BEAT_S / 2 + i / 4) % 1;
    ctx.strokeStyle = `rgba(${['255,111,165', '165,123,255', '79,182,255', '140,220,190'][i]},${0.32 * (1 - p)})`;
    ctx.lineWidth = 10 * (1 - p) + 2;
    ctx.beginPath();
    ctx.arc(960, 470, 120 + p * 760, 0, Math.PI * 2);
    ctx.stroke();
  }
  // the latest line with 布比 in it, so the characters stay up between lines
  const lines = LYRICS.filter(l => l.t0 - 0.2 <= GT && l.chars.some(c => /[布比]/.test(c[0])) && l.t0 >= (o.from || 0));
  const line = lines[lines.length - 1];
  if (line) {
    const toks = line.chars.filter(c => /[布比]/.test(c[0])).slice(0, 8);
    const per = Math.min(190, 1500 / toks.length);
    toks.forEach(([c, at], i) => {
      const k = backOut(clamp((GT - at) / 0.25));
      if (k <= 0) return;
      const x = 960 + (i - (toks.length - 1) / 2) * per, y = 400 + Math.sin(GT * 6 + i) * 6;
      ctx.save();
      ctx.translate(x, y);
      const sc = k * (1 + 0.1 * kick) * (per / 190);
      ctx.scale(sc, sc);
      ctx.rotate((i % 2 ? 1 : -1) * 0.07);
      text(ctx, c, 0, 60, { size: 180, weight: 900, gradient: [BRAND[i % 3], BRAND[(i + 1) % 3]] });
      ctx.restore();
    });
  }
  const b = easeOut(seg(t, o.titleAt ?? 0.3, (o.titleAt ?? 0.3) + 0.5));
  if (b > 0) {
    ctx.save();
    ctx.translate(960, 630);
    ctx.scale(1 + 0.05 * kick, 1 + 0.05 * kick);
    text(ctx, 'Boopie', 0, 40, { size: 120, weight: 800, gradient: BRAND, alpha: b, ls: -3, blur: (1 - b) * 8 });
    ctx.restore();
    if (o.sub) text(ctx, o.sub, 960, 750, { size: 44, weight: 700, color: '#2a2a2f', alpha: easeOut(seg(t, (o.titleAt ?? 0.3) + 0.3, (o.titleAt ?? 0.3) + 0.8)), ls: 6 });
  }
  const bounce = Math.abs(Math.sin(beatPhase() * Math.PI));
  const sq = Math.sin(beatPhase() * Math.PI) * 0.14 - 0.05;
  const pb = backOut(seg(t, 0.1, 0.6)), pm = backOut(seg(t, 0.25, 0.75));
  if (pb > 0) drawBoopie(ctx, 330, 900 - 50 * bounce, 260 * pb, pose(t, { squash: sq, happy: true, wave: 1 }));
  if (pm > 0) drawMuse(ctx, 1600, 930 - 40 * Math.abs(Math.sin(beatPhase() * Math.PI + 1.2)), 360 * pm, pose(t, { squash: sq, wave: 1 }));
}

// ---------------------------------------------------------------- 早安对我眨眨眼 天气它都记得清
function sunIcon(ctx, x, y, r, t) {
  ctx.save();
  ctx.translate(x, y);
  ctx.rotate(t * 0.8);
  ctx.strokeStyle = '#ffb300';
  ctx.lineWidth = r * 0.16;
  ctx.lineCap = 'round';
  for (let i = 0; i < 8; i++) {
    ctx.rotate(Math.PI / 4);
    ctx.beginPath();
    ctx.moveTo(r * 1.3, 0);
    ctx.lineTo(r * 1.7, 0);
    ctx.stroke();
  }
  const g = ctx.createRadialGradient(-r * 0.3, -r * 0.3, 0, 0, 0, r);
  g.addColorStop(0, '#fff3a0');
  g.addColorStop(1, '#ffc107');
  ctx.fillStyle = g;
  ctx.beginPath();
  ctx.arc(0, 0, r, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}
function sMorning(ctx, t) {
  title(ctx, '早安，今天也一起', 'Good morning', 960, 130, t, 0.05, 3.5, { size: 72 });
  const k = easeOut(seg(t, 0, 0.5));
  device(ctx, 760, 560, 250 * lerp(0.9, 1, k), { screen: screenClip(t > 1.3 ? 'boopie_happy' : 'boopie_idle', t) });
  // a wink right on 眨眨眼
  const wink = GT - sungAt('早安', 4);
  if (wink > 0 && wink < 0.6) {
    const s = backOut(clamp(wink / 0.25));
    ctx.save();
    ctx.translate(980, 360);
    ctx.scale(s, s);
    ctx.rotate(-0.2);
    text(ctx, '✦', 0, 0, { size: 70, color: '#ffc93c', alpha: 1 - clamp((wink - 0.35) / 0.25) });
    ctx.restore();
  }
  const b = backOut(clamp((GT - sungAt('早安', 0)) / 0.3));
  if (b > 0) {
    ctx.save();
    ctx.translate(440, 330);
    ctx.scale(b, b);
    card(ctx, -130, -50, 260, 100, 50);
    text(ctx, '早安呀！', 0, 14, { size: 40, weight: 800, gradient: BRAND });
    ctx.restore();
  }
  // the weather, on 天气
  const w = backOut(clamp((GT - sungAt('天气', 0)) / 0.35));
  if (w > 0) {
    ctx.save();
    ctx.translate(1340, 560);
    ctx.scale(w, w);
    card(ctx, -220, -170, 440, 340, 44);
    sunIcon(ctx, -90, -40, 52, t);
    text(ctx, '26°', 90, 0, { size: 110, weight: 800, align: 'center', gradient: ['#ff9f43', '#ff6fa5'] });
    text(ctx, '晴 · 适合出去走走', 0, 120, { size: 30, weight: 600, color: '#6e6e73' });
    ctx.restore();
  }
}

// ---------------------------------------------------------------- 小小的它 装着大大的宇宙
function sCosmos(ctx, t) {
  const grow = easeInOut(seg(t, 1.0, 3.0));
  // the universe pouring out of the little screen
  for (let i = 0; i < 160; i++) {
    const a = hash(i, 1) * Math.PI * 2, sp = 0.4 + hash(i, 2);
    const d = ((t * 180 * sp + hash(i, 3) * 900) % 900) * (0.2 + grow);
    const x = 960 + Math.cos(a) * d, y = 560 + Math.sin(a) * d * 0.75;
    const c = ['255,111,165', '165,123,255', '79,182,255', '255,201,60'][i % 4];
    ctx.fillStyle = `rgba(${c},${clamp(d / 300) * 0.8})`;
    ctx.beginPath();
    ctx.arc(x, y, 1.5 + hash(i, 4) * 3.5 * (0.5 + grow), 0, Math.PI * 2);
    ctx.fill();
  }
  const kick = beatKick();
  const R = lerp(120, 260, grow);
  device(ctx, 960, 560, R, { screen: screenClip(t < 1.4 ? 'boopie_idle' : 'skin_boopie_starry', t), sweep: seg(t, 1.0, 2.2) });
}

// ---------------------------------------------------------------- 亮起粉色的心: a heart out of the screen
function heartBurst(ctx, at, x, y) {
  const q = (GT - at) / 1.0;
  if (q <= 0 || q >= 1) return;
  const s = backOut(clamp(q * 3)) * (1 + q * 0.4);
  ctx.save();
  ctx.globalAlpha = 1 - easeIn(q);
  ctx.translate(x, y - q * 120);
  ctx.scale(s, s);
  const g = ctx.createLinearGradient(-60, -60, 60, 60);
  g.addColorStop(0, '#ff9ac0');
  g.addColorStop(1, '#ff4f8b');
  ctx.fillStyle = g;
  ctx.shadowColor = 'rgba(255,80,140,0.5)';
  ctx.shadowBlur = 30;
  ctx.beginPath();
  ctx.moveTo(0, 40);
  ctx.bezierCurveTo(-90, -20, -40, -90, 0, -40);
  ctx.bezierCurveTo(40, -90, 90, -20, 0, 40);
  ctx.fill();
  ctx.restore();
}
function sCommandSong(ctx, t) {
  sCommand(ctx, t);
  const at = sungAt('works', 0), q = GT - at;
  if (q > 0) {
    const k = backOut(clamp(q / 0.35)), kick = beatKick();
    ctx.save();
    ctx.translate(560, 820);
    ctx.scale(k * (1 + 0.05 * kick), k * (1 + 0.05 * kick));
    ctx.rotate(-0.04);
    text(ctx, 'Works with Muse', 0, 0, { size: 84, weight: 900, gradient: BRAND, ls: -1 });
    ctx.restore();
  }
}
function sProductSong(ctx, t) {
  sProduct(ctx, t);
  heartBurst(ctx, sungAt('亮起', 5), 640, 380);
}

// ---------------------------------------------------------------- 带我去它的小世界: rooms on the words
// [when, room]: 世界 → home, 种花 → the farm, 钓鱼 → the sea, 冒险 → the woods, 下雨 → rain, 节日 → festival.
function worldPlan() {
  return [
    [0, 0], [sungAt('带我', 0), 2], [sungAt('种花', 0), 3], [sungAt('种花', 2), 5], [sungAt('种花', 4), 4],
    [sungAt('下雨', 0), 6], [sungAt('节日', 0), 7], [sungAt('一年', 0), 1],
  ];
}
function sWorldSong(ctx, t) {
  title(ctx, '它有自己的小世界', 'A tiny world inside', 470, 250, t, 0.1, 10.6, { size: 72 });
  const plan = worldPlan();
  let i = 0;
  for (let j = 0; j < plan.length; j++) if (GT >= plan[j][0] || j === 0) i = j;
  const room = plan[i][1], since = GT - plan[i][0];
  ROOMS.forEach(([, name], j) => {
    const k = easeOut(seg(t, 0.3 + j * 0.05, 0.7 + j * 0.05));
    if (k <= 0) return;
    const x = 200 + (j % 4) * 140, y = 420 + Math.floor(j / 4) * 80;
    const on = j === room, pop = on ? backOut(clamp(since / 0.25)) : 1;
    pill(ctx, name, x + 60, y, { size: 26, alpha: k, fill: on ? '#6b4fd8' : 'rgba(255,255,255,0.9)', color: on ? '#fff' : '#3a3a3f',
      scale: on ? 1 + 0.15 * (1 - pop) + 0.08 : 1 });
  });
  const wk = easeOut(clamp(since / 0.3));
  text(ctx, ROOMS[room][2], 470, 660, { size: 34, weight: 600, color: '#3a3a3f', alpha: wk * seg(t, 0.4, 0.7), dy: (1 - wk) * 16 });
  const k = easeOut(seg(t, 0, 0.6));
  const kick = beatKick();
  const cx = 1260, cy = 590, R0 = 300;
  device(ctx, cx, cy + (1 - k) * 60, R0 * lerp(0.92, 1, k), { screen: (c, r) => {
    const sw = easeInOut(clamp(since / 0.3));
    const one = (j, lt, dx) => drawClip(c, ROOMS[j][0], lt, dx, 0, r, { square: 1 });
    if (i > 0 && sw < 1) {
      one(plan[i - 1][1], since + 2, -sw * 2 * r);
      one(room, since, (1 - sw) * 2 * r);
    } else one(room, since + 0.5, 0);
  } });
  const pb = backOut(seg(t, 0.6, 1.1));
  if (pb > 0) drawBoopie(ctx, cx - 250, cy - 268 - 18 * Math.abs(Math.sin(beatPhase() * Math.PI)), 130 * pb, pose(t, { look: 0.6, happy: true }));
}

// ---------------------------------------------------------------- 布比布比 一直在你身旁
function sStay(ctx, t) {
  sMoods(ctx, t);
  for (let i = 0; i < 14; i++) {   // hearts floating up
    const q = ((t * 0.35) + i / 14) % 1;
    const x = 200 + hash(i, 7) * 1500, y = 1000 - q * 900;
    ctx.save();
    ctx.globalAlpha = Math.sin(q * Math.PI) * 0.7 * seg(t, 0.3, 0.8);
    ctx.translate(x, y);
    ctx.scale(0.35 + hash(i, 8) * 0.3, 0.35 + hash(i, 8) * 0.3);
    ctx.fillStyle = BRAND[i % 3];
    ctx.beginPath();
    ctx.moveTo(0, 40);
    ctx.bezierCurveTo(-90, -20, -40, -90, 0, -40);
    ctx.bezierCurveTo(40, -90, 90, -20, 0, 40);
    ctx.fill();
    ctx.restore();
  }
}

// ---------------------------------------------------------------- the end, the two of them dancing
function sOutroSong(ctx, t) {
  sOutro(ctx, t);
}

// ---------------------------------------------------------------- the timeline, on the lyrics
// [start, end, scene, natural length (it plays faster or slower to fit), punch: a hit on the way in].
const SONG_TIMELINE = [
  [0, 7.4, sIntro, 6.6],
  [7.4, 16.6, (c, t) => sChant(c, t, { titleAt: 0.4, sub: '你的 AI 小伙伴' }), 9.2],
  [16.6, 20.2, sProductSong, 3.8],
  [20.2, 23.8, sMorning, 3.6],
  [23.8, 27.2, sSetup, 3.6],
  [27.2, 31.2, sConnect, 4.6],
  [31.2, 34.9, sMuse, 7.0],
  [34.9, 38.75, sRemote, 7.2],
  [38.75, 42.3, (c, t) => sChant(c, t, { titleAt: 1.3, sub: '你的 AI 小伙伴', from: 38 }), 3.55, true],
  [42.3, 45.4, sVoice, 5.2],
  [45.4, 49.4, sCommandSong, 4.0],
  [49.4, 52.7, sCosmos, 3.3],
  [52.7, 56.8, sSkins, 6.4, true],
  [56.8, 60.7, sGames, 5.0],
  [60.7, 71.5, sWorldSong, 10.8, true],
  [71.5, 77.8, sStay, 5.0],
  [77.8, 90.6, sOutroSong, 11.0, true],
];
// The choruses, for a harder beat.
const CHORUS = [[38.75, 52.7], [60.7, 77.8], [77.8, 90.6]];
const inChorus = t => CHORUS.some(([a, b]) => t >= a && t < b);

