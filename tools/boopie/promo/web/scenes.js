// The promo's scenes, each drawn at its own time t (seconds from its start).

const pose = (t, o = {}) => ({ t, blink: (t % 3.1) < 0.12 ? 1 : 0, ...o });

// ---------------------------------------------------------------- 1. Boopie, Muse, the gadget
function sIntro(ctx, t) {
  const move = easeInOut(seg(t, 2.6, 3.6));
  const cx = 960, cy = lerp(520, 395, move), R = lerp(250, 190, move);
  // a line of light drawing the ring
  const draw = easeInOut(seg(t, 0, 1.3));
  if (t < 2.2) {
    ctx.save();
    ctx.globalAlpha = 1 - seg(t, 1.5, 2.2);
    const g = ctx.createConicGradient(-Math.PI / 2, cx, cy);
    g.addColorStop(0, BRAND[0]);
    g.addColorStop(0.5, BRAND[1]);
    g.addColorStop(1, BRAND[2]);
    ctx.strokeStyle = g;
    ctx.lineWidth = 6;
    ctx.lineCap = 'round';
    ctx.shadowColor = 'rgba(165,123,255,0.6)';
    ctx.shadowBlur = 30;
    ctx.beginPath();
    ctx.arc(cx, cy, R * 1.16, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * draw);
    ctx.stroke();
    ctx.restore();
  }
  const k = easeOut(seg(t, 0.9, 1.9));
  if (k > 0) {
    ctx.save();
    ctx.globalAlpha = k;
    device(ctx, cx, cy, R, { scale: lerp(0.9, 1, k), on: seg(t, 1.6, 2.1), sweep: seg(t, 1.8, 3.0),
      screen: screenClip('boopie_idle', t - 1.6) });
    ctx.restore();
  }
  // the two of them pop out either side
  const pb = backOut(seg(t, 2.9, 3.5)), pm = backOut(seg(t, 3.15, 3.75));
  if (pb > 0) drawBoopie(ctx, 400, 800, 270 * pb, pose(t, { wave: seg(t, 4, 4.3) * (1 - seg(t, 5.6, 6)), happy: t > 4.2 && t < 5.4 }));
  if (pm > 0) drawMuse(ctx, 1530, 830, 400 * pm, pose(t + 1.3, { wave: seg(t, 4.3, 4.6) * (1 - seg(t, 5.8, 6.2)) }));
  const a = easeOut(seg(t, 3.4, 4.2));
  if (a > 0) {
    text(ctx, 'Boopie', 960, 790, { size: 168, weight: 800, gradient: BRAND, alpha: a, dy: (1 - a) * 50, ls: -4, blur: (1 - a) * 10 });
    const a2 = easeOut(seg(t, 3.8, 4.6));
    text(ctx, '你的 AI 小伙伴', 960, 870, { size: 44, weight: 600, color: '#3a3a3f', alpha: a2, dy: (1 - a2) * 30, ls: 4 });
    const a3 = backOut(seg(t, 4.3, 4.9));
    if (a3 > 0) pill(ctx, 'Works with Muse', 960, 948, { size: 26, scale: a3, color: '#6b4fd8' });
  }
}

// ---------------------------------------------------------------- 2. the gadget itself
function sProduct(ctx, t) {
  const rot = Math.sin(seg(t, 0.2, 3.6) * Math.PI) * 0.95;
  const k = easeOut(seg(t, 0, 0.7));
  const shows = ['skin_boopie_starry', 'skin_boopie_jellyfish', 'skin_muse_astronaut', 'skin_whale_koi'];
  device(ctx, 640, 540, 250 * lerp(0.92, 1, k), { rot, sweep: seg(t, 0.5, 2.2),
    screen: screenClip(shows[Math.min(3, Math.floor(t / 1.0))], t) });
  title(ctx, '精致小圆，握在掌心', 'Small. Round. Always there.', 1080, 300, t, 0.2, 4.4, { align: 'left', size: 72 });
  const specs = [['1.75″', 'AMOLED 圆形屏'], ['466×466', '高清像素'], ['ESP32-S3', '双核 AI 芯片'], ['Wi-Fi + BLE', '无线连接'],
    ['触摸 · 体感', '还能听会说']];
  specs.forEach(([a, b], i) => {
    const q = easeOut(seg(t, 0.8 + i * 0.25, 1.4 + i * 0.25)) * (1 - seg(t, 4.0, 4.4));
    if (q <= 0) return;
    const x = 1080 + (i % 2) * 360, y = 420 + Math.floor(i / 2) * 150;
    ctx.save();
    ctx.globalAlpha = q;
    ctx.translate(0, (1 - q) * 30);
    card(ctx, x, y, 330, 120, 26);
    text(ctx, a, x + 28, y + 58, { size: 38, weight: 800, align: 'left', gradient: i === 2 ? BRAND : null });
    text(ctx, b, x + 28, y + 96, { size: 22, weight: 500, align: 'left', color: '#6e6e73' });
    ctx.restore();
  });
}

// ---------------------------------------------------------------- 3. scan to set up
// The setup screen with its QR code and hotspot password frosted over: never shown.
const QR = [128, 96, 210, 210], PW = [150, 318, 166, 52];
function frost(c, im, src, dx, dy, dw, dh) {
  c.save();
  c.beginPath();
  c.rect(dx, dy, dw, dh);
  c.clip();
  c.filter = `blur(${Math.max(6, dw / 14)}px)`;
  if (im.complete) c.drawImage(im, src[0], src[1], src[2], src[3], dx, dy, dw, dh);
  c.filter = 'none';
  c.fillStyle = 'rgba(235,238,245,0.55)';
  c.fillRect(dx, dy, dw, dh);
  c.restore();
}
const setupScreen = t => (c, r) => {
  drawClip(c, 'setup_qr', t, 0, 0, r);
  const im = clipFrame('setup_qr', t), k = (2 * r) / 466;
  for (const b of [QR, PW]) frost(c, im, b, -r + b[0] * k, -r + b[1] * k, b[2] * k, b[3] * k);
};
function sSetup(ctx, t) {
  title(ctx, '扫一扫，就上线', 'Set up in seconds', 960, 150, t, 0.1, 3.6);
  const k = easeOut(seg(t, 0, 0.6));
  const done = t > 2.4;
  device(ctx, 680, 600, 240 * lerp(0.94, 1, k), {
    screen: done ? screenClip('boopie_happy', t - 2.4) : setupScreen(t) });
  const p = easeOut(seg(t, 0.3, 1.0));
  const px = lerp(1600, 1240, p);
  phone(ctx, px, 610, 640, (c, w, h) => {
    c.fillStyle = '#e9ebf0';
    c.fillRect(-w / 2, -h / 2, w, h);
    // the camera sees the QR code on the gadget
    const im = clipFrame('setup_qr', t);
    c.save();
    c.beginPath();
    c.rect(-w / 2, -h / 2 + 60, w, h - 160);
    c.clip();
    c.imageSmoothingEnabled = false;
    frost(c, im, QR, -w * 0.4, -h * 0.28, w * 0.8, w * 0.8);
    c.restore();
    const fr = w * 0.8;
    c.strokeStyle = '#2fd07a';
    c.lineWidth = 6;
    for (const [sx, sy] of [[-1, -1], [1, -1], [-1, 1], [1, 1]]) {
      c.beginPath();
      const x0 = sx * fr / 2, y0 = -h * 0.28 + fr / 2 + sy * fr / 2;
      c.moveTo(x0, y0 - sy * 40);
      c.lineTo(x0, y0);
      c.lineTo(x0 - sx * 40, y0);
      c.stroke();
    }
    if (t < 1.9) {
      const ly = -h * 0.28 + fr * ((t * 0.9) % 1);
      const lg = c.createLinearGradient(0, ly - 30, 0, ly);
      lg.addColorStop(0, 'rgba(47,208,122,0)');
      lg.addColorStop(1, 'rgba(47,208,122,0.7)');
      c.fillStyle = lg;
      c.fillRect(-fr / 2, ly - 30, fr, 30);
    }
    const ok = easeOut(seg(t, 1.9, 2.3));
    if (ok > 0) {
      c.globalAlpha = ok;
      card(c, -w * 0.42, h * 0.3, w * 0.84, 110, 22);
      text(c, '已识别 Boopie', -w * 0.42 + 30, h * 0.3 + 48, { size: 26, weight: 700, align: 'left' });
      text(c, '正在发送 Wi-Fi…', -w * 0.42 + 30, h * 0.3 + 84, { size: 20, weight: 500, align: 'left', color: '#6e6e73' });
      c.globalAlpha = 1;
    }
  });
  // dots flying from the phone to the gadget
  for (let i = 0; i < 14; i++) {
    const q = seg(t, 2.0 + i * 0.03, 2.5 + i * 0.03);
    if (q <= 0 || q >= 1) continue;
    const x = lerp(1180, 720, q), y = 600 - Math.sin(q * Math.PI) * 220 + (hash(i) - 0.5) * 40;
    ctx.fillStyle = `rgba(${i % 2 ? '79,182,255' : '165,123,255'},${1 - q * 0.5})`;
    ctx.beginPath();
    ctx.arc(x, y, 7, 0, Math.PI * 2);
    ctx.fill();
  }
  const b = backOut(seg(t, 2.5, 2.9));
  if (b > 0) pill(ctx, '已联网', 680, 920, { scale: b, color: '#1a9b57', size: 30 });
}

// ---------------------------------------------------------------- 4. Wi-Fi, Bluetooth, VPN
function wifiIcon(ctx, x, y, s, c) {
  ctx.save();
  ctx.strokeStyle = c;
  ctx.lineWidth = s * 0.12;
  ctx.lineCap = 'round';
  for (let i = 1; i <= 3; i++) {
    ctx.beginPath();
    ctx.arc(x, y + s * 0.35, s * 0.25 * i, -Math.PI * 0.75, -Math.PI * 0.25);
    ctx.stroke();
  }
  ctx.fillStyle = c;
  ctx.beginPath();
  ctx.arc(x, y + s * 0.35, s * 0.08, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}
function btIcon(ctx, x, y, s, c) {
  ctx.save();
  ctx.strokeStyle = c;
  ctx.lineWidth = s * 0.1;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(x - s * 0.25, y - s * 0.2);
  ctx.lineTo(x + s * 0.25, y + s * 0.2);
  ctx.lineTo(x, y + s * 0.42);
  ctx.lineTo(x, y - s * 0.42);
  ctx.lineTo(x + s * 0.25, y - s * 0.2);
  ctx.lineTo(x - s * 0.25, y + s * 0.2);
  ctx.stroke();
  ctx.restore();
}
function shieldIcon(ctx, x, y, s, c) {
  ctx.save();
  ctx.fillStyle = c;
  ctx.beginPath();
  ctx.moveTo(x, y - s * 0.5);
  ctx.quadraticCurveTo(x + s * 0.25, y - s * 0.35, x + s * 0.42, y - s * 0.38);
  ctx.quadraticCurveTo(x + s * 0.45, y + s * 0.25, x, y + s * 0.5);
  ctx.quadraticCurveTo(x - s * 0.45, y + s * 0.25, x - s * 0.42, y - s * 0.38);
  ctx.quadraticCurveTo(x - s * 0.25, y - s * 0.35, x, y - s * 0.5);
  ctx.fill();
  ctx.strokeStyle = '#fff';
  ctx.lineWidth = s * 0.09;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(x - s * 0.17, y);
  ctx.lineTo(x - s * 0.03, y + s * 0.14);
  ctx.lineTo(x + s * 0.2, y - s * 0.12);
  ctx.stroke();
  ctx.restore();
}
function check(ctx, x, y, s, k) {
  if (k <= 0) return;
  ctx.save();
  ctx.translate(x, y);
  ctx.scale(backOut(k), backOut(k));
  ctx.fillStyle = '#2fd07a';
  ctx.beginPath();
  ctx.arc(0, 0, s, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = '#fff';
  ctx.lineWidth = s * 0.22;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(-s * 0.4, 0);
  ctx.lineTo(-s * 0.1, s * 0.3);
  ctx.lineTo(s * 0.42, -s * 0.3);
  ctx.stroke();
  ctx.restore();
}
function sConnect(ctx, t) {
  title(ctx, '随时随地，畅通连接', 'Wi-Fi  ·  Bluetooth  ·  VPN', 960, 140, t, 0.1, 4.6);
  const scr = t < 0.9 ? 'wifi' : t < 1.7 ? 'bluetooth' : 'vpn';
  device(ctx, 520, 600, 230, { screen: screenClip(scr, t) });
  const cards = [[0.3, 900, 'Wi-Fi', '已连接', wifiIcon], [0.9, 1330, '蓝牙', '手机已连接', btIcon]];
  cards.forEach(([at, x, name, st, icon], i) => {
    const q = easeOut(seg(t, at, at + 0.5));
    if (q <= 0) return;
    ctx.save();
    ctx.globalAlpha = q;
    ctx.translate(0, (1 - q) * 30);
    card(ctx, x, 270, 400, 150, 30);
    icon(ctx, x + 70, 335, 64, '#4f7cff');
    text(ctx, name, x + 130, 335, { size: 36, weight: 700, align: 'left' });
    text(ctx, st, x + 130, 375, { size: 22, weight: 500, align: 'left', color: '#6e6e73' });
    check(ctx, x + 350, 345, 22, seg(t, at + 0.4, at + 0.7));
    ctx.restore();
  });
  // the star: VPN
  const q = easeOut(seg(t, 1.6, 2.2));
  if (q > 0) {
    ctx.save();
    ctx.globalAlpha = q;
    ctx.translate(0, (1 - q) * 40);
    const x = 900, y = 450, w = 830, h = 420;
    card(ctx, x, y, w, h, 36, { stroke: 'rgba(165,123,255,0.5)', lw: 3, blur: 60 });
    const on = seg(t, 2.4, 2.7);
    shieldIcon(ctx, x + 78, y + 82, 78, on > 0.5 ? '#6b4fd8' : '#b8b8c2');
    text(ctx, 'VPN', x + 140, y + 98, { size: 52, weight: 800, align: 'left', gradient: on > 0.5 ? BRAND : null, color: '#1d1d1f' });
    // the switch
    const sx = x + w - 170, sy = y + 52;
    ctx.fillStyle = on > 0.5 ? '#2fd07a' : '#d6d6dc';
    ctx.beginPath();
    roundRect(ctx, sx, sy, 120, 64, 32);
    ctx.fill();
    ctx.fillStyle = '#fff';
    ctx.shadowColor = 'rgba(0,0,0,0.2)';
    ctx.shadowBlur = 8;
    ctx.beginPath();
    ctx.arc(sx + 32 + easeInOut(on) * 56, sy + 32, 26, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;
    const st = on < 1 ? '未连接' : t < 3.0 ? '连接中…' : '已连接 · 安全加密';
    text(ctx, st, x + 140, y + 140, { size: 26, weight: 500, align: 'left', color: on < 1 ? '#8e8e93' : '#1a9b57' });
    // nodes
    const nodes = [['节点一', 86], ['节点二', 142], ['节点三', 203]];
    nodes.forEach(([n, ms], i) => {
      const r = easeOut(seg(t, 2.7 + i * 0.15, 3.1 + i * 0.15));
      if (r <= 0) return;
      const ny = y + 190 + i * 70;
      ctx.globalAlpha = q * r;
      ctx.fillStyle = i === 0 ? 'rgba(107,79,216,0.08)' : 'rgba(0,0,0,0.03)';
      ctx.beginPath();
      roundRect(ctx, x + 40, ny, w - 80, 58, 18);
      ctx.fill();
      text(ctx, n, x + 80, ny + 39, { size: 26, weight: i === 0 ? 700 : 500, align: 'left' });
      const shown = Math.round(ms * easeOut(seg(t, 2.9 + i * 0.15, 3.6 + i * 0.15)));
      text(ctx, `${shown} ms`, x + w - 140, ny + 39, { size: 26, weight: 600, align: 'right', color: ms < 100 ? '#1a9b57' : '#8e8e93' });
      if (i === 0) check(ctx, x + w - 85, ny + 29, 16, seg(t, 3.4, 3.7));
    });
    ctx.restore();
  }
}

// ---------------------------------------------------------------- 5. Works with Muse
function sMuse(ctx, t) {
  title(ctx, 'Works with Muse', '连接 Muse，一句话就懂你', 960, 170, t, 0.1, 7.8, { size: 96, gradient: BRAND, ls: -2 });
  const dissolve = seg(t, 5.6, 7.0);
  const pm = backOut(seg(t, 0.4, 1.0));
  const mx = 560, my = 900, mh = 520;
  if (pm > 0 && dissolve < 1) {
    ctx.save();
    ctx.globalAlpha = 1 - easeIn(dissolve);
    const talk = t > 3.2 && t < 5.2 ? Math.abs(Math.sin(t * 11)) * 0.8 : 0;
    drawMuse(ctx, mx, my, mh * pm * (1 - dissolve * 0.3), pose(t, { talk, wave: seg(t, 1.0, 1.3) * (1 - seg(t, 2.6, 3.0)), look: 0.4 }));
    ctx.restore();
  }
  // pixels of it flying into the screen
  if (dissolve > 0 && dissolve < 1) {
    for (let i = 0; i < 160; i++) {
      const d = clamp((dissolve - hash(i, 9) * 0.5) * 2);
      if (d <= 0 || d >= 1) continue;
      const sx = mx + (hash(i, 1) - 0.5) * 320, sy = my - hash(i, 2) * 500;
      const e = easeInOut(d);
      const x = lerp(sx, 1340, e), y = lerp(sy, 560, e) - Math.sin(e * Math.PI) * 120;
      ctx.fillStyle = ['#f2e3c8', '#e2cfa8', '#fff3e2', '#d8c29a', '#f2e3c8', '#2a1a14'][i % 6];
      const s = lerp(14, 6, e);
      ctx.fillRect(x - s / 2, y - s / 2, s, s);
    }
  }
  // the link
  const link = seg(t, 1.2, 2.8);
  const linked = t > 2.8;
  const screenName = t < 2.8 ? 'muse_idle' : t < 5.6 ? 'muse_speaking' : 'muse_happy';
  // pulse rings round the gadget while pairing
  if (link > 0 && !linked) {
    for (let i = 0; i < 3; i++) {
      const p = ((t * 0.9) + i / 3) % 1;
      ctx.strokeStyle = `rgba(165,123,255,${0.45 * (1 - p)})`;
      ctx.lineWidth = 4;
      ctx.beginPath();
      ctx.arc(1340, 560, 280 + p * 140, 0, Math.PI * 2);
      ctx.stroke();
    }
  }
  if (link > 0 && dissolve < 0.4) {
    const x0 = mx + 150, y0 = 620, x1 = 1080, y1 = 560;
    ctx.save();
    ctx.globalAlpha = 1 - seg(dissolve, 0, 0.4);
    const g = ctx.createLinearGradient(x0, 0, x1, 0);
    g.addColorStop(0, 'rgba(255,111,165,0.0)');
    g.addColorStop(0.5, 'rgba(165,123,255,0.8)');
    g.addColorStop(1, 'rgba(79,182,255,0.9)');
    ctx.strokeStyle = g;
    ctx.lineWidth = linked ? 8 : 5;
    ctx.lineCap = 'round';
    ctx.shadowColor = 'rgba(165,123,255,0.7)';
    ctx.shadowBlur = 24;
    ctx.setLineDash(linked ? [] : [2, 18]);
    ctx.lineDashOffset = -t * 120;
    ctx.beginPath();
    const e = easeOut(link);
    ctx.moveTo(x0, y0);
    ctx.quadraticCurveTo((x0 + x1) / 2, y0 - 200, lerp(x0, x1, e), lerp(y0, y1, e));
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.shadowBlur = 0;
    for (let i = 0; i < 10; i++) {   // bits of data along it
      const q = ((t * 0.7) + i / 10) % 1;
      if (q > e) continue;
      const a = 1 - q;
      const bx = (1 - q) * (1 - q) * x0 + 2 * (1 - q) * q * ((x0 + x1) / 2) + q * q * x1;
      const by = (1 - q) * (1 - q) * y0 + 2 * (1 - q) * q * (y0 - 200) + q * q * y1;
      ctx.fillStyle = i % 2 ? '#ff6fa5' : '#4fb6ff';
      ctx.beginPath();
      ctx.arc(bx, by, 6 * (0.6 + a * 0.4), 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }
  const dk = easeOut(seg(t, 0.2, 0.9));
  device(ctx, 1340, 560, 240 * lerp(0.9, 1, dk), { screen: screenClip(screenName, t - (t < 2.8 ? 0 : 2.8)),
    sweep: seg(t, 2.8, 3.6) });
  const st = linked ? '已连接 Muse' : '配对中…';
  const pa = easeOut(seg(t, 1.2, 1.6)) * (1 - seg(t, 7.4, 7.9));
  if (pa > 0) pill(ctx, st, 1340, 900, { alpha: pa, color: linked ? '#1a9b57' : '#6b4fd8', size: 28,
    scale: linked ? backOut(seg(t, 2.8, 3.2)) : 1 });
  // its hello, from Muse
  const hb = backOut(seg(t, 3.2, 3.6)) * (1 - seg(t, 5.4, 5.8));
  if (hb > 0) {
    ctx.save();
    ctx.translate(820, 420);
    ctx.scale(hb, hb);
    card(ctx, -10, -60, 300, 100, 30);
    text(ctx, '你好呀！', 140, 6, { size: 34, weight: 700 });
    ctx.restore();
  }
}

// ---------------------------------------------------------------- 6. talking
function bubble(ctx, s, x, y, k, o = {}) {
  if (k <= 0) return;
  ctx.save();
  ctx.translate(x, y);
  ctx.scale(k, k);
  ctx.font = `600 ${o.size || 32}px ${SANS}`;
  const w = ctx.measureText(s).width + 70, h = (o.size || 32) * 2.4;
  const x0 = o.right ? -w : 0;
  card(ctx, x0, -h / 2, w, h, h / 2, { fill: o.fill || '#fff' });
  ctx.fillStyle = o.color || '#1d1d1f';
  ctx.textAlign = 'left';
  ctx.textBaseline = 'middle';
  ctx.fillText(s, x0 + 35, 2);
  ctx.restore();
}
function sVoice(ctx, t) {
  title(ctx, '会听 · 会说 · 会陪你', 'Talk, naturally', 960, 140, t, 0.1, 5.8);
  const name = t < 1.6 ? 'listening' : t < 2.4 ? 'thinking' : t < 5.0 ? 'speaking' : 'boopie_happy';
  device(ctx, 960, 590, 250, { screen: screenClip(name, t) });
  bubble(ctx, '今天天气怎么样？', 230, 420, backOut(seg(t, 0.3, 0.7)), { fill: '#6b4fd8', color: '#fff' });
  // the voice, as bars
  if (t < 1.8) {
    for (let i = 0; i < 24; i++) {
      const hgt = (0.3 + 0.7 * Math.abs(Math.sin(t * 9 + i * 0.7) * Math.sin(t * 4.3 + i))) * 70 * seg(t, 0.2, 0.5) * (1 - seg(t, 1.5, 1.8));
      ctx.fillStyle = BRAND[i % 3];
      ctx.beginPath();
      roundRect(ctx, 240 + i * 16, 520 - hgt / 2, 8, Math.max(8, hgt), 4);
      ctx.fill();
    }
  }
  bubble(ctx, '今天晴，最高 26℃', 1690, 560, backOut(seg(t, 2.6, 3.0)), { right: true });
  bubble(ctx, '适合出去走走～', 1690, 670, backOut(seg(t, 3.0, 3.4)), { right: true });
  const f = backOut(seg(t, 4.9, 5.3));
  if (f > 0) pill(ctx, '实时字幕 · 中英双语', 960, 950, { scale: f, color: '#6b4fd8' });
}

// ---------------------------------------------------------------- 7. it does what it's told
function sCommand(ctx, t) {
  title(ctx, '说一句，它就去做', 'Just say it', 560, 300, t, 0.1, 3.9, { size: 76 });
  const name = t < 0.5 ? 'boopie_idle' : t < 2.1 ? 'tool_noise' : 'nest_living';
  device(ctx, 1280, 560, 250, { screen: screenClip(name, t < 2.1 ? t : t - 2.1) });
  const cmds = [['“放点雨声”', 0.2, 500], ['“去小窝看看”', 1.9, 640]];
  cmds.forEach(([s, at, y]) => {
    const k = backOut(seg(t, at, at + 0.4));
    if (k > 0) pill(ctx, s, 560, y, { scale: k, size: 34, color: '#1d1d1f' });
    const a = seg(t, at + 0.3, at + 0.6);
    if (a > 0 && a < 1) {          // a streak to the gadget
      ctx.strokeStyle = `rgba(165,123,255,${1 - a})`;
      ctx.lineWidth = 5;
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.moveTo(lerp(760, 1000, a - 0.2 < 0 ? 0 : a - 0.2), y);
      ctx.lineTo(lerp(760, 1000, a), lerp(y, 560, a));
      ctx.stroke();
    }
  });
  const k = backOut(seg(t, 0.6, 0.9)) * (1 - seg(t, 1.9, 2.1));
  if (k > 0) pill(ctx, '正在播放雨声', 1280, 900, { scale: k, color: '#4f7cff' });
  const k2 = backOut(seg(t, 2.3, 2.6));
  if (k2 > 0) pill(ctx, '已打开小窝', 1280, 900, { scale: k2, color: '#1a9b57' });
}

// ---------------------------------------------------------------- 8. characters and skins
const CHARS = [['char_boopie', '布比'], ['char_muse', 'Muse'], ['char_gpt', 'GPT'], ['char_codex', 'Codex'],
  ['char_klaude', '小克'], ['char_whale', '小鲸鱼'], ['char_doubao', '豆包']];
function sSkins(ctx, t) {
  title(ctx, 'One Boopie, 40+ looks', '7 个角色  ·  40 多套皮肤', 960, 130, t, 0.1, 7.8, { size: 80 });
  let name, label;
  if (t < 3.5) {
    const i = clamp(Math.floor(t / 0.5), 0, 6);
    [name, label] = CHARS[i];
  } else {
    const i = Math.floor((t - 3.5) / 0.35) % SKINS.length;
    name = 'skin_' + SKINS[i];
    label = null;
  }
  // the ring of skins round it
  const ring = seg(t, 3.3, 4.6);
  if (ring > 0) {
    const n = 28;
    for (let i = 0; i < n; i++) {
      const a = (i / n) * Math.PI * 2 + t * 0.25;
      const appear = backOut(clamp((ring * n - i) / 3));
      if (appear <= 0) continue;
      const depth = (Math.sin(a) + 1) / 2;   // nearer at the bottom
      const x = 960 + Math.cos(a) * 660, y = 560 + Math.sin(a) * 255;
      const r = (38 + depth * 34) * appear;
      miniScreen(ctx, 'skin_' + SKINS[(i * 3) % SKINS.length], t + i * 0.2, x, y, r, { alpha: 0.55 + depth * 0.45 });
    }
  }
  const pulse = t < 3.5 ? 1 + 0.03 * Math.exp(-((t % 0.5) * 10)) : 1;
  device(ctx, 960, 560, 220 * pulse, { screen: screenClip(name, t) });
  if (label) pill(ctx, label, 960, 880, { size: 32, scale: 1 + 0.1 * Math.exp(-((t % 0.5) * 12)) });
  const big = backOut(seg(t, 5.6, 6.1));
  if (big > 0) {
    ctx.save();
    ctx.translate(1700, 190);
    ctx.scale(big, big);
    text(ctx, '40+', 0, 30, { size: 110, weight: 900, gradient: BRAND, ls: -3 });
    ctx.restore();
  }
}

// ---------------------------------------------------------------- 9. moods
const MOODS = [['pet_hungry', '饿了'], ['pet_eating', '吃饭'], ['pet_sleepy', '犯困'], ['pet_sad', '委屈'], ['pet_dizzy', '晕乎乎'],
  ['ov_surprise', '惊讶'], ['ov_blush', '害羞'], ['ov_confetti', '开心'], ['ov_hearts', '比心']];
function sMoods(ctx, t) {
  title(ctx, '有情绪，会撒娇', 'Full of feelings', 470, 520, t, 0.1, 3.9, { size: 72 });
  MOODS.forEach(([name, label], i) => {
    const k = backOut(seg(t, 0.2 + i * 0.08, 0.6 + i * 0.08));
    if (k <= 0) return;
    const x = 1020 + (i % 3) * 270, y = 330 + Math.floor(i / 3) * 255;
    const bob = Math.sin(t * 2 + i) * 6;
    miniScreen(ctx, name, t, x, y + bob, 96 * k);
    text(ctx, label, x, y + 135 + bob, { size: 26, weight: 600, color: '#3a3a3f', alpha: k });
  });
}

// ---------------------------------------------------------------- 10. games
const GAMES = [['game_whack', '戳戳布比', 'Tap'], ['game_catch', '接零食', 'Tilt'], ['game_maze', '重力迷宫', 'Tilt'],
  ['game_hop', '跳跳布比', 'Tap']];
function sGames(ctx, t) {
  title(ctx, '4 款小游戏', 'Tap  ·  Tilt  ·  Play', 960, 150, t, 0.1, 5.8, { size: 80 });
  GAMES.forEach(([name, label, how], i) => {
    const k = backOut(seg(t, 0.3 + i * 0.15, 0.8 + i * 0.15));
    if (k <= 0) return;
    const x = 300 + i * 440, y = 560;
    const tilt = how === 'Tilt' ? Math.sin(t * 2.4 + i) * 0.14 : 0;
    const tap = how === 'Tap' ? 1 - 0.03 * Math.abs(Math.sin(t * 5 + i)) : 1;
    device(ctx, x, y, 165 * k * tap, { tilt, screen: screenClip(name, t + 0.6) });
    text(ctx, label, x, y + 290, { size: 34, weight: 700, alpha: k });
    pill(ctx, how === 'Tap' ? '点一点' : '歪一歪', x, y + 350, { size: 22, alpha: k, color: '#6b4fd8' });
  });
  // stars rising
  for (let i = 0; i < 12; i++) {
    const q = ((t * 0.5) + i / 12) % 1;
    const x = 200 + hash(i, 4) * 1520, y = 900 - q * 700;
    text(ctx, '★', x, y, { size: 28 + hash(i, 6) * 20, color: '#ffc93c', alpha: Math.sin(q * Math.PI) * seg(t, 1, 1.5) });
  }
}

// ---------------------------------------------------------------- 11. its little world
const ROOMS = [['nest_living', '客厅', '装扮自己的小窝'], ['nest_bedroom', '卧室', '晚上会自己去睡觉'],
  ['nest_outside', '院子', '散步、喂鸡、看花'], ['nest_farm', '农场', '种花种菜，收获换星星'],
  ['nest_slime', '森林', '打史莱姆、开宝箱'], ['nest_swim', '海边', '游泳、钓鱼、捡贝壳'],
  ['nest_rain', '下雨天', '跟着真实天气，下雨会回家'], ['nest_xmas', '节日', '春节、圣诞……节日自动装饰']];
function sWorld(ctx, t) {
  title(ctx, '它有自己的小世界', 'A tiny world inside', 470, 250, t, 0.1, 11.8, { size: 72 });
  const per = 1.45, i = Math.min(ROOMS.length - 1, Math.floor(Math.max(0, t - 0.4) / per));
  const local = Math.max(0, t - 0.4) - i * per;
  // the room list, current one lit
  ROOMS.forEach(([, name], j) => {
    const k = easeOut(seg(t, 0.4 + j * 0.06, 0.9 + j * 0.06));
    if (k <= 0) return;
    const x = 200 + (j % 4) * 140, y = 420 + Math.floor(j / 4) * 80;
    const on = j === i;
    pill(ctx, name, x + 60, y, { size: 26, alpha: k, fill: on ? '#6b4fd8' : 'rgba(255,255,255,0.9)', color: on ? '#fff' : '#3a3a3f',
      scale: on ? 1.08 : 1 });
  });
  const [, , what] = ROOMS[i];
  const wk = easeOut(seg(local, 0, 0.3));
  text(ctx, what, 470, 660, { size: 34, weight: 600, color: '#3a3a3f', alpha: wk * seg(t, 0.5, 0.8), dy: (1 - wk) * 16 });
  // the round screen, the world in it, sliding from room to room
  const k = easeOut(seg(t, 0, 0.8));
  const R0 = 330, cx = 1260, cy = 560;
  device(ctx, cx, cy + (1 - k) * 60, R0 * lerp(0.92, 1, k), { screen: (c, r) => {
    const sw = easeInOut(seg(local, 0, 0.35));
    const one = (j, lt, dx) => drawClip(c, ROOMS[j][0], lt, dx, 0, r, { square: 1, zoom: 1.0 });
    if (i > 0 && sw < 1) {
      one(i - 1, local + per, -sw * 2 * r);
      one(i, local, (1 - sw) * 2 * r);
    } else one(i, local, 0);
  } });
  // Boopie peeking over its rim
  const pb = backOut(seg(t, 0.9, 1.4));
  if (pb > 0) drawBoopie(ctx, cx - 250, cy - 268, 130 * pb, pose(t, { look: 0.6, happy: (t % 4) > 3 }));
}

// ---------------------------------------------------------------- 12. the end
function sOutro(ctx, t) {
  // skins flying in to the middle
  for (let i = 0; i < 26; i++) {
    const q = easeInOut(seg(t, 0 + i * 0.03, 1.4 + i * 0.03));
    if (q >= 1) continue;
    const a = hash(i, 2) * Math.PI * 2, d = 900 + hash(i, 3) * 300;
    const x = 960 + Math.cos(a) * d * (1 - q), y = 430 + Math.sin(a) * d * 0.6 * (1 - q);
    miniScreen(ctx, 'skin_' + SKINS[i % SKINS.length], t, x, y, 60 * (1 - q * 0.8), { alpha: 1 - easeIn(q) });
  }
  const dk = backOut(seg(t, 1.2, 1.8));
  if (dk > 0) device(ctx, 960, 400, 200 * dk, { screen: screenClip('boopie_confetti', t - 1.2), sweep: seg(t, 1.6, 2.6) });
  const pb = backOut(seg(t, 2.0, 2.5)), pm = backOut(seg(t, 2.2, 2.7));
  if (pb > 0) drawBoopie(ctx, 470, 860, 280 * pb, pose(t, { wave: seg(t, 2.6, 2.9), happy: t > 4 }));
  if (pm > 0) drawMuse(ctx, 1450, 880, 400 * pm, pose(t + 0.7, { wave: seg(t, 2.8, 3.1) }));
  const a = easeOut(seg(t, 2.6, 3.4));
  if (a > 0) {
    text(ctx, 'Boopie', 960, 780, { size: 150, weight: 800, gradient: BRAND, alpha: a, dy: (1 - a) * 40, ls: -4, blur: (1 - a) * 8 });
    const a2 = easeOut(seg(t, 3.0, 3.8));
    text(ctx, '你的 AI 小伙伴', 960, 860, { size: 48, weight: 700, color: '#2a2a2f', alpha: a2, dy: (1 - a2) * 24, ls: 6 });
    const a3 = backOut(seg(t, 3.5, 4.0));
    if (a3 > 0) pill(ctx, 'Works with Muse', 960, 940, { scale: a3, color: '#6b4fd8', size: 26 });
    text(ctx, '概念演示 · 画面为模拟渲染', 1880, 1050, { size: 18, weight: 500, color: '#a1a1a6', align: 'right', alpha: seg(t, 4, 4.5) });
  }
}

// ---------------------------------------------------------------- the song's beat
// The song (song/boopie.mp3): 129 BPM, its first downbeat at 1.0 s. GT is the video's time.
let GT = 0;
const BEAT_S = 60 / 129.2, BEAT0 = 1.0;
const beatPhase = () => { const b = (GT - BEAT0) / BEAT_S; return b - Math.floor(b); };
const beatKick = () => Math.exp(-beatPhase() * 6);      // 1 on the beat, falling away

// ---------------------------------------------------------------- the chorus: 布比布比 Boopie
function sChorus(ctx, t) {
  const kick = beatKick();
  // rings pulsing out on the beat
  for (let i = 0; i < 4; i++) {
    const p = ((GT - BEAT0) / BEAT_S / 2 + i / 4) % 1;
    ctx.strokeStyle = `rgba(${['255,111,165', '165,123,255', '79,182,255', '140,220,190'][i]},${0.35 * (1 - p)})`;
    ctx.lineWidth = 10 * (1 - p) + 2;
    ctx.beginPath();
    ctx.arc(960, 470, 120 + p * 700, 0, Math.PI * 2);
    ctx.stroke();
  }
  // 布 比 布 比, a character a beat
  const chars = ['布', '比', '布', '比'];
  chars.forEach((c, i) => {
    const k = backOut(seg(t, 0.15 + i * BEAT_S, 0.45 + i * BEAT_S));
    if (k <= 0) return;
    const x = 960 + (i - 1.5) * 190, y = 380 + Math.sin(t * 6 + i) * 6;
    ctx.save();
    ctx.translate(x, y);
    const sc = k * (1 + 0.08 * kick);
    ctx.scale(sc, sc);
    ctx.rotate((i % 2 ? 1 : -1) * 0.06);
    text(ctx, c, 0, 60, { size: 180, weight: 900, gradient: [BRAND[i % 3], BRAND[(i + 1) % 3]] });
    ctx.restore();
  });
  const b = easeOut(seg(t, 2.0, 2.6));
  if (b > 0) {
    ctx.save();
    ctx.translate(960, 600);
    ctx.scale(1 + 0.04 * kick, 1 + 0.04 * kick);
    text(ctx, 'Boopie', 0, 40, { size: 130, weight: 800, gradient: BRAND, alpha: b, ls: -3, blur: (1 - b) * 8 });
    ctx.restore();
    text(ctx, '你的 AI 小伙伴', 960, 720, { size: 46, weight: 700, color: '#2a2a2f', alpha: easeOut(seg(t, 2.5, 3.1)), ls: 6 });
  }
  // the two of them bouncing to it
  const sq = Math.sin(beatPhase() * Math.PI) * 0.12 - 0.04;
  const pb = backOut(seg(t, 0.3, 0.8)), pm = backOut(seg(t, 0.5, 1.0));
  if (pb > 0) drawBoopie(ctx, 330, 960 - 40 * Math.abs(Math.sin(beatPhase() * Math.PI)), 260 * pb, pose(t, { squash: sq, happy: true, wave: 1 }));
  if (pm > 0) drawMuse(ctx, 1600, 990 - 30 * Math.abs(Math.sin(beatPhase() * Math.PI + 1)), 360 * pm, pose(t, { squash: sq, wave: 1 }));
}

// ---------------------------------------------------------------- Muse on the phone, a computer far away, Boopie in step
function museApp(c, w, h, t) {
  // the Muse app: dark, its avatar on top, a chat, an input bar and tabs
  c.fillStyle = '#121214';
  c.fillRect(-w / 2, -h / 2, w, h);
  const top = -h / 2;
  c.save();
  c.beginPath();
  c.arc(0, top + 92, 46, 0, Math.PI * 2);
  c.fillStyle = '#e9e1d2';
  c.fill();
  c.clip();
  drawMuse(c, 0, top + 175, 150, pose(t, { bow: false }));
  c.restore();
  pill(c, 'muse', 0, top + 150, { size: 18, fill: 'rgba(60,60,64,0.95)', color: '#fff', pad: 30 });
  // the chat
  const bub = (s, right, y, k, o = {}) => {
    if (k <= 0) return;
    c.save();
    c.globalAlpha = k;
    c.font = `500 20px ${SANS}`;
    const lines = s.split('\n'), bw = Math.max(...lines.map(l => c.measureText(l).width)) + 36, bh = 22 + lines.length * 28;
    const x = right ? w / 2 - 24 - bw : -w / 2 + 24;
    c.translate(0, (1 - k) * 12);
    c.fillStyle = right ? '#3a3a40' : '#232326';
    c.beginPath();
    roundRect(c, x, y, bw, bh, 18);
    c.fill();
    c.fillStyle = o.color || '#f2f2f4';
    c.textAlign = 'left';
    lines.forEach((l, i) => c.fillText(l, x + 18, y + 34 + i * 28));
    c.restore();
  };
  const typed = '帮我把这周的照片整理成相册';
  const n = Math.floor(clamp((t - 0.5) / 0.9) * typed.length);
  bub(typed.slice(0, Math.max(1, n)), true, top + 190, seg(t, 0.4, 0.6));
  bub('好的～我用电脑帮你弄', false, top + 260, easeOut(seg(t, 1.6, 1.9)));
  // the computer far away, in a window
  const wk = easeOut(seg(t, 2.1, 2.5));
  if (wk > 0) {
    const x0 = -w / 2 + 22, y0 = top + 330, ww = w - 44, wh = h - 560;
    c.save();
    c.globalAlpha = wk;
    c.translate(0, (1 - wk) * 20);
    c.fillStyle = '#e9edf5';
    c.beginPath();
    roundRect(c, x0, y0, ww, wh, 16);
    c.fill();
    c.save();
    c.clip();
    // its desktop: a menu bar, a folder of photos, an album
    c.fillStyle = 'rgba(255,255,255,0.8)';
    c.fillRect(x0, y0, ww, 22);
    ['#ff5f57', '#febc2e', '#28c840'].forEach((cc, i) => { c.fillStyle = cc; c.beginPath(); c.arc(x0 + 14 + i * 14, y0 + 11, 4.5, 0, 7); c.fill(); });
    text(c, '远程电脑', x0 + ww / 2, y0 + 16, { size: 13, weight: 600, color: '#555' });
    c.fillStyle = '#fff';
    c.beginPath();
    roundRect(c, x0 + 12, y0 + 34, ww * 0.56, wh - 46, 10);
    c.fill();
    const prog = clamp((t - 2.6) / 3.4);
    const moved = Math.floor(prog * 12);
    const cols = ['#ffb3c7', '#b9e4ff', '#ffe08a', '#c8f2c2', '#d9c8ff', '#ffd0a8'];
    for (let i = 0; i < 12; i++) {
      if (i < moved) continue;
      const gx = x0 + 22 + (i % 4) * 44, gy = y0 + 44 + Math.floor(i / 4) * 52;
      c.fillStyle = cols[i % 6];
      c.beginPath();
      roundRect(c, gx, gy, 36, 42, 5);
      c.fill();
    }
    // the album, filling up
    const ax = x0 + ww * 0.62, ay = y0 + 40;
    c.fillStyle = '#fff';
    c.beginPath();
    roundRect(c, ax, ay, ww * 0.34, wh - 60, 10);
    c.fill();
    text(c, '相册', ax + ww * 0.17, ay + 24, { size: 14, weight: 700, color: '#444' });
    for (let i = 0; i < moved; i++) {
      c.fillStyle = cols[i % 6];
      c.fillRect(ax + 10 + (i % 3) * 30, ay + 36 + Math.floor(i / 3) * 30, 24, 24);
    }
    // the photo on its way, and the pointer moving it
    const ph = (prog * 12) % 1, k2 = easeInOut(ph);
    if (prog < 1) {
      const i = moved, sx = x0 + 40 + (i % 4) * 44, sy = y0 + 65 + Math.floor(i / 4) * 52;
      const ex = ax + 22 + (i % 3) * 30, ey = ay + 48 + Math.floor(i / 3) * 30;
      const px = lerp(sx, ex, k2), py = lerp(sy, ey, k2) - Math.sin(k2 * Math.PI) * 30;
      c.fillStyle = cols[i % 6];
      c.fillRect(px - 14, py - 16, 28, 32);
      c.fillStyle = '#111';
      c.beginPath();
      c.moveTo(px + 6, py + 4);
      c.lineTo(px + 6, py + 26);
      c.lineTo(px + 12, py + 20);
      c.lineTo(px + 20, py + 22);
      c.closePath();
      c.fill();
    }
    c.restore();
    c.restore();
    // live
    const live = 0.5 + 0.5 * Math.sin(t * 8);
    c.fillStyle = `rgba(255,70,70,${0.6 + 0.4 * live})`;
    c.beginPath();
    c.arc(x0 + 8, y0 + wh + 22, 5, 0, 7);
    c.fill();
    text(c, prog < 1 ? `Muse 正在远程操作… ${moved}/12` : '已完成 · 12 张照片整理好了', x0 + 20, y0 + wh + 29,
      { size: 17, weight: 500, align: 'left', color: prog < 1 ? '#c9c9ce' : '#5be39a' });
  }
  // input bar and tabs
  c.fillStyle = '#232326';
  c.beginPath();
  roundRect(c, -w / 2 + 18, h / 2 - 150, w - 36, 54, 27);
  c.fill();
  text(c, '+   发消息', -w / 2 + 44, h / 2 - 115, { size: 19, weight: 500, align: 'left', color: '#8a8a90' });
  c.fillStyle = '#1c1c1f';
  c.beginPath();
  roundRect(c, -w / 2 + 18, h / 2 - 82, w - 36, 60, 30);
  c.fill();
  for (let i = 0; i < 5; i++) {
    c.strokeStyle = i === 0 ? '#f2f2f4' : '#8a8a90';
    c.lineWidth = 2.5;
    c.beginPath();
    roundRect(c, -w / 2 + 50 + i * (w - 100) / 4.6, h / 2 - 64, 22, 22, 6);
    c.stroke();
  }
}
function sRemote(ctx, t) {
  title(ctx, '手机一句话，电脑去执行', '布比实时告诉你进度', 960, 120, t, 0.1, 7.2, { size: 66 });
  const k = easeOut(seg(t, 0, 0.6));
  phone(ctx, lerp(380, 560, k), 530, 820, (c, w, h) => museApp(c, w, h, t));
  const prog = clamp((t - 2.6) / 3.4), done = prog >= 1;
  // the link between them
  if (t > 1.8) {
    for (let i = 0; i < 12; i++) {
      const q = ((t * 0.8) + i / 12) % 1;
      const x = lerp(760, 1150, q), y = 560 - Math.sin(q * Math.PI) * 120;
      ctx.fillStyle = i % 2 ? 'rgba(255,111,165,0.85)' : 'rgba(79,182,255,0.85)';
      ctx.beginPath();
      ctx.arc(x, y, 6, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  const dk = easeOut(seg(t, 0.2, 0.8));
  device(ctx, 1390, 560, 250 * lerp(0.92, 1, dk), { screen: (c, r) => {
    drawClip(c, done ? 'muse_happy' : 'muse_idle', t, 0, 0, r);
    if (t > 2.2) {
      // a ring of progress round the screen, a badge and a count
      c.strokeStyle = done ? '#2fd07a' : '#a57bff';
      c.lineWidth = r * 0.05;
      c.lineCap = 'round';
      c.beginPath();
      c.arc(0, 0, r * 0.95, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * prog);
      c.stroke();
      if (!done) pill(c, `整理相册 ${Math.floor(prog * 12)}/12`, 0, r * 0.66, { size: r * 0.075, fill: 'rgba(30,30,40,0.85)', color: '#fff' });
    }
  } });
  const bk = backOut(seg(t, 2.2, 2.6));
  if (bk > 0) pill(ctx, done ? '完成！' : 'Muse 联动中', 1390, 920, { scale: bk, size: 28, fill: done ? '#2fd07a' : '#6b4fd8', color: '#fff' });
  if (done) {
    for (let i = 0; i < 40; i++) {   // confetti
      const q = clamp((t - 6.0) * 0.9 - hash(i, 3) * 0.2);
      if (q <= 0 || q >= 1) continue;
      const a = hash(i, 1) * Math.PI * 2, d = 120 + hash(i, 2) * 260;
      ctx.fillStyle = ['#ff6fa5', '#a57bff', '#4fb6ff', '#ffc93c', '#2fd07a'][i % 5];
      ctx.save();
      ctx.translate(1390 + Math.cos(a) * d * easeOut(q), 580 + Math.sin(a) * d * easeOut(q) + q * q * 120);
      ctx.rotate(q * 8 + i);
      ctx.globalAlpha = 1 - q;
      ctx.fillRect(-6, -3, 12, 6);
      ctx.restore();
    }
  }
}

// ---------------------------------------------------------------- the timeline
// [start, end, scene, its natural length]: a scene plays faster or slower to fit
// the song's sections (song/boopie.mp3, 90.9 s; bars 1.858 s from 1.0 s).
const TIMELINE_75 = [
  [0, 6.6, sIntro, 6.6], [6.6, 10.3, sProduct, 4], [10.3, 14.0, sSetup, 3.6], [14.0, 17.7, sConnect, 4.6],
  [17.7, 25.2, sMuse, 8], [25.2, 32.6, sRemote, 7.4],
  [32.6, 37.2, sChorus, 4.6], [37.2, 43.0, sVoice, 6], [43.0, 47.6, sCommand, 4], [47.6, 53.0, sMoods, 4.4],
  [53.0, 58.6, sSkins, 7.6], [58.6, 64.2, sGames, 5.6], [64.2, 79.0, sWorld, 12.4], [79.0, 90.6, sOutro, 11.6],
];

