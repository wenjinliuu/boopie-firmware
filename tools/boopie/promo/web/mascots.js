// Smooth, high-resolution Boopie and Muse for the promo video, drawn on a
// canvas. Each takes the centre of its feet (x, y), a height, and a pose:
// { t, blink 0..1, wave 0..1, squash 0..1 (+ squash, - stretch), look -1..1, happy }.

function softShadow(ctx, x, y, w) {
  const g = ctx.createRadialGradient(x, y, 0, x, y, w);
  g.addColorStop(0, 'rgba(60,40,80,0.22)');
  g.addColorStop(1, 'rgba(60,40,80,0)');
  ctx.save();
  ctx.scale(1, 0.22);
  ctx.fillStyle = g;
  ctx.beginPath();
  ctx.arc(x, y / 0.22, w, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function eyes(ctx, cx, cy, gap, w, h, blink, look, happy) {
  for (const s of [-1, 1]) {
    const ex = cx + s * gap + look * w * 0.5;
    ctx.save();
    if (happy) {           // ^ ^
      ctx.strokeStyle = '#2a1a22';
      ctx.lineWidth = w * 0.55;
      ctx.lineCap = 'round';
      ctx.beginPath();
      ctx.arc(ex, cy + h * 0.25, w * 0.95, Math.PI * 1.15, Math.PI * 1.85);
      ctx.stroke();
    } else {
      const k = Math.max(0.08, 1 - blink);
      ctx.fillStyle = '#21161c';
      ctx.beginPath();
      ctx.ellipse(ex, cy, w, h * k, 0, 0, Math.PI * 2);
      ctx.fill();
      if (k > 0.5) {
        ctx.fillStyle = '#fff';
        ctx.beginPath();
        ctx.ellipse(ex - w * 0.3, cy - h * 0.35 * k, w * 0.38, h * 0.22 * k, 0, 0, Math.PI * 2);
        ctx.fill();
      }
    }
    ctx.restore();
  }
}

function cheeks(ctx, cx, cy, gap, r, a) {
  for (const s of [-1, 1]) {
    const g = ctx.createRadialGradient(cx + s * gap, cy, 0, cx + s * gap, cy, r);
    g.addColorStop(0, `rgba(255,110,140,${a})`);
    g.addColorStop(1, 'rgba(255,110,140,0)');
    ctx.fillStyle = g;
    ctx.beginPath();
    ctx.ellipse(cx + s * gap, cy, r, r * 0.65, 0, 0, Math.PI * 2);
    ctx.fill();
  }
}

// Boopie: a pink jelly, flared at the bottom, its sides like little arms, an antenna with a bulb.
function drawBoopie(ctx, x, y, h, p = {}) {
  const t = p.t || 0, sq = p.squash || 0, wave = p.wave || 0;
  const w = h * 1.05 * (1 + sq * 0.12), hh = h * (1 - sq * 0.12);
  softShadow(ctx, x, y + h * 0.02, w * 0.55);
  ctx.save();
  ctx.translate(x, y);
  const top = -hh * 0.82;
  // the antenna
  const sway = Math.sin(t * 2.2) * 0.12;
  const ax = Math.sin(sway) * hh * 0.28, ay = top - hh * 0.26;
  ctx.strokeStyle = '#c23a62';
  ctx.lineWidth = h * 0.028;
  ctx.lineCap = 'round';
  ctx.beginPath();
  ctx.moveTo(0, top + hh * 0.05);
  ctx.quadraticCurveTo(ax * 0.2, top - hh * 0.12, ax, ay);
  ctx.stroke();
  const bg = ctx.createRadialGradient(ax - h * 0.02, ay - h * 0.025, h * 0.005, ax, ay, h * 0.075);
  bg.addColorStop(0, '#ffffff');
  bg.addColorStop(0.35, '#ffd6e2');
  bg.addColorStop(1, '#e8436f');
  ctx.fillStyle = bg;
  ctx.beginPath();
  ctx.arc(ax, ay, h * 0.07, 0, Math.PI * 2);
  ctx.fill();
  const glow = ctx.createRadialGradient(ax, ay, 0, ax, ay, h * 0.2);
  glow.addColorStop(0, 'rgba(255,120,170,0.35)');
  glow.addColorStop(1, 'rgba(255,120,170,0)');
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.arc(ax, ay, h * 0.2, 0, Math.PI * 2);
  ctx.fill();
  // the body: a soft round mochi, a little wider low down, with stubby arms
  const lw = -w / 2, rw = w / 2;
  const grad = () => {
    const g = ctx.createRadialGradient(lw * 0.3, top + hh * 0.25, h * 0.05, 0, top + hh * 0.5, w * 0.7);
    g.addColorStop(0, '#ffd3e3');
    g.addColorStop(0.45, '#ff95bb');
    g.addColorStop(1, '#ec5b8f');
    return g;
  };
  const body = new Path2D();
  body.moveTo(lw * 0.82, -hh * 0.02);
  body.bezierCurveTo(lw * 1.08, -hh * 0.06, lw * 1.04, top + hh * 0.42, lw * 0.82, top + hh * 0.22);
  body.bezierCurveTo(lw * 0.6, top + hh * 0.02, lw * 0.3, top, 0, top);
  body.bezierCurveTo(rw * 0.3, top, rw * 0.6, top + hh * 0.02, rw * 0.82, top + hh * 0.22);
  body.bezierCurveTo(rw * 1.04, top + hh * 0.42, rw * 1.08, -hh * 0.06, rw * 0.82, -hh * 0.02);
  body.quadraticCurveTo(0, hh * 0.03, lw * 0.82, -hh * 0.02);
  body.closePath();
  ctx.fillStyle = grad();
  ctx.shadowColor = 'rgba(236,91,143,0.25)';
  ctx.shadowBlur = h * 0.08;
  ctx.fill(body);
  ctx.shadowBlur = 0;
  for (const s of [-1, 1]) {   // arms
    const lift = s < 0 ? wave * (1 + Math.sin(t * 9) * 0.3) : 0;
    ctx.save();
    ctx.translate(s * w * 0.47, top + hh * 0.6);
    ctx.rotate(-s * (0.5 + lift * 1.9));
    ctx.fillStyle = grad();
    ctx.beginPath();
    ctx.ellipse(0, h * 0.08, h * 0.07, h * 0.11, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = 'rgba(200,50,110,0.35)';
    ctx.lineWidth = h * 0.008;
    ctx.stroke();
    ctx.restore();
  }
  // jelly shine
  ctx.save();
  ctx.clip(body);
  const sh = ctx.createLinearGradient(lw * 0.5, top, lw * 0.1, top + hh * 0.4);
  sh.addColorStop(0, 'rgba(255,255,255,0.85)');
  sh.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = sh;
  ctx.beginPath();
  ctx.ellipse(lw * 0.42, top + hh * 0.26, w * 0.14, hh * 0.09, -0.7, 0, Math.PI * 2);
  ctx.fill();
  const rim = ctx.createLinearGradient(0, -hh * 0.25, 0, 0);
  rim.addColorStop(0, 'rgba(200,40,100,0)');
  rim.addColorStop(1, 'rgba(200,40,100,0.25)');
  ctx.fillStyle = rim;
  ctx.fillRect(lw * 1.2, -hh * 0.3, w * 1.2, hh * 0.3);
  ctx.restore();
  // the face
  const fy = top + hh * 0.56;
  eyes(ctx, 0, fy, w * 0.14, h * 0.038, h * 0.075, p.blink || 0, p.look || 0, p.happy);
  cheeks(ctx, 0, fy + h * 0.09, w * 0.25, h * 0.07, 0.55);
  ctx.strokeStyle = '#3a1a28';
  ctx.lineWidth = h * 0.022;
  ctx.lineCap = 'round';
  ctx.beginPath();
  const my = fy + h * 0.1, mw = h * 0.035;
  if (p.talk) {
    ctx.fillStyle = '#8a2a48';
    ctx.beginPath();
    ctx.ellipse(0, my + h * 0.01, mw * 1.1, mw * (0.4 + p.talk), 0, 0, Math.PI * 2);
    ctx.fill();
  } else {
    ctx.arc(-mw, my - mw * 0.3, mw, 0.1 * Math.PI, 0.9 * Math.PI);
    ctx.moveTo(mw * 2, my - mw * 0.3);
    ctx.arc(mw, my - mw * 0.3, mw, 0.1 * Math.PI, 0.9 * Math.PI);
    ctx.stroke();
  }
  ctx.restore();
}

// Muse: a tall beige hood, a round cream face, big dark eyes, stubby arms and feet.
function drawMuse(ctx, x, y, h, p = {}) {
  const t = p.t || 0, sq = p.squash || 0, wave = p.wave || 0;
  const w = h * 0.66 * (1 + sq * 0.1), hh = h * (1 - sq * 0.1);
  softShadow(ctx, x, y + h * 0.01, w * 0.62);
  ctx.save();
  ctx.translate(x, y);
  const top = -hh;
  const fur = (a, b) => {
    const g = ctx.createLinearGradient(-w / 2, top, w / 2, 0);
    g.addColorStop(0, a);
    g.addColorStop(1, b);
    return g;
  };
  // feet
  ctx.fillStyle = '#cdb994';
  for (const s of [-1, 1]) {
    ctx.beginPath();
    ctx.ellipse(s * w * 0.2, -hh * 0.02, w * 0.16, hh * 0.04, 0, 0, Math.PI * 2);
    ctx.fill();
  }
  // the hood and body
  const body = new Path2D();
  body.moveTo(0, top);
  body.bezierCurveTo(w * 0.42, top, w * 0.5, top + hh * 0.2, w * 0.5, top + hh * 0.42);
  body.bezierCurveTo(w * 0.52, top + hh * 0.7, w * 0.5, -hh * 0.02, w * 0.3, -hh * 0.03);
  body.lineTo(-w * 0.3, -hh * 0.03);
  body.bezierCurveTo(-w * 0.5, -hh * 0.02, -w * 0.52, top + hh * 0.7, -w * 0.5, top + hh * 0.42);
  body.bezierCurveTo(-w * 0.5, top + hh * 0.2, -w * 0.42, top, 0, top);
  ctx.fillStyle = fur('#f6ecd9', '#d9c6a2');
  ctx.shadowColor = 'rgba(120,90,50,0.2)';
  ctx.shadowBlur = h * 0.06;
  ctx.fill(body);
  ctx.shadowBlur = 0;
  ctx.save();
  ctx.clip(body);
  const sh = ctx.createRadialGradient(-w * 0.2, top + hh * 0.12, 0, -w * 0.2, top + hh * 0.12, w * 0.45);
  sh.addColorStop(0, 'rgba(255,255,255,0.6)');
  sh.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = sh;
  ctx.fillRect(-w, top, w * 2, hh);
  ctx.restore();
  // arms, sticking out at the sides
  for (const s of [-1, 1]) {
    ctx.save();
    const lift = s < 0 ? wave * (1 + Math.sin(t * 10) * 0.3) : 0;
    ctx.translate(s * w * 0.5, top + hh * 0.53);
    ctx.rotate(-s * (0.45 + lift * 2.0));
    ctx.fillStyle = fur('#efe3cc', '#d3bf98');
    ctx.beginPath();
    ctx.ellipse(0, hh * 0.09, w * 0.1, hh * 0.12, 0, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = 'rgba(150,120,80,0.35)';
    ctx.lineWidth = h * 0.006;
    ctx.stroke();
    ctx.restore();
  }
  // a pink bow on the hood
  if (p.bow !== false) {
    const bx = w * 0.28, by = top + hh * 0.17;
    ctx.fillStyle = '#f7c6cf';
    for (const s of [-1, 1]) {
      ctx.beginPath();
      ctx.ellipse(bx + s * h * 0.03, by, h * 0.03, h * 0.02, s * 0.5, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.fillStyle = '#eaa5b2';
    ctx.beginPath();
    ctx.arc(bx, by, h * 0.012, 0, Math.PI * 2);
    ctx.fill();
  }
  // the face
  const fcy = top + hh * 0.35;
  const fg = ctx.createRadialGradient(-w * 0.08, fcy - hh * 0.06, 0, 0, fcy, w * 0.34);
  fg.addColorStop(0, '#fff3e2');
  fg.addColorStop(1, '#f2ddbf');
  ctx.fillStyle = fg;
  ctx.beginPath();
  ctx.ellipse(0, fcy, w * 0.33, hh * 0.19, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.strokeStyle = 'rgba(170,140,100,0.35)';
  ctx.lineWidth = h * 0.006;
  ctx.stroke();
  eyes(ctx, 0, fcy - hh * 0.01, w * 0.13, h * 0.027, h * 0.03, p.blink || 0, p.look || 0, p.happy);
  cheeks(ctx, 0, fcy + hh * 0.06, w * 0.2, h * 0.04, 0.5);
  ctx.strokeStyle = '#3a2a20';
  ctx.lineWidth = h * 0.012;
  ctx.lineCap = 'round';
  if (p.talk) {
    ctx.fillStyle = '#7a3a30';
    ctx.beginPath();
    ctx.ellipse(0, fcy + hh * 0.075, h * 0.018, h * 0.008 + p.talk * h * 0.014, 0, 0, Math.PI * 2);
    ctx.fill();
  } else {
    ctx.beginPath();
    ctx.arc(0, fcy + hh * 0.055, h * 0.022, 0.15 * Math.PI, 0.85 * Math.PI);
    ctx.stroke();
  }
  ctx.restore();
}
