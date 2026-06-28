/** Vista previa canvas de gestos (tema oscuro CRT, alineado con firmware/face.h). */
(function (global) {
  const W = 320, H = 200;
  const BG = '#000000';
  const VISOR = '#102030';
  const VISOR_DEEP = '#081018';
  const INK = '#00e8ff';
  const INK_GLOW = '#006688';
  const INK_BRIGHT = '#88ffff';
  const EYE = '#f0ece8';
  const EYE_GLOW = '#224466';
  const PUP = '#000000';
  const HILITE = '#ffffff';
  const RED = '#ff2020';
  const BLUE = '#4488ff';
  const PURPLE = '#aa44ff';

  const EYE_L = 100, EYE_R = 220, EYE_Y = 68, EYE_RAD = 34, CXC = 160;

  const EMOTIONS = [
    'neutral', 'happy', 'sad', 'angry', 'surprised', 'thinking', 'sleepy',
    'love', 'excited', 'cool', 'confused', 'dizzy', 'vibing',
  ];

  const PRESETS = {
    neutral: { brow: 'flat', topLid: 0.12, botLid: 0, mouthBow: 0, mouthSegs: 7 },
    happy: { brow: 'arch', topLid: 0.08, botLid: 0.22, mouthBow: 0.28, mouthSegs: 7 },
    sad: { brow: 'sad', lidAng: 'sad-out', topLid: 0.05, tear: true, mouthBow: -0.22, mouthSegs: 7 },
    angry: { brow: 'angry', lidAng: 'angry-in', topLid: 0.38, botLid: 0.38, mouthBow: -0.12, mouthSegs: 5,
      mouth: { x0: 88, x1: 232, gap: 17, teeth: 6, bow: -4, pinch: 0.5 } },
    surprised: { brow: 'raise', eyeR: 38, pupilS: 7, mouthBow: 0.05, mouthSegs: 8 },
    thinking: { brow: 'asym', topLid: 0.18, pupilOffX: 8, pupilOffY: -5, mouthBow: 0, mouthSegs: 7 },
    sleepy: { brow: 'flat', topLid: 0.55, mouthBow: -0.08, mouthSegs: 6 },
    love: { brow: 'arch', eyeR: 36, botLid: 0.12, pupil: 'heart', mouthBow: 0.28, mouthSegs: 7 },
    excited: { brow: 'raise', eyeR: 37, pupilS: 8, mouthBow: 0.28, mouthSegs: 7 },
    cool: { brow: 'arch', winkRight: true, topLid: 0.1, mouthBow: 0.1, mouthSegs: 7 },
    confused: { brow: 'asym', topLidL: 0.28, topLidR: 0.1, mouthBow: 0, mouthSegs: 7 },
    dizzy: { brow: 'flat', pupil: 'spiral', mouthBow: 0, mouthSegs: 7 },
    vibing: { brow: 'arch', topLid: 0.06, botLid: 0.20, mouthBow: 0.30, mouthSegs: 7 },
  };

  const MOUTH_COLORS = {
    angry: ['#fc6060', '#884400', '#fe8080'],
    love: ['#ff99cc', '#884488', '#ffccff'],
    sad: ['#66aaff', '#002244', '#88ccff'],
    sleepy: ['#44aaff', '#001122', '#66ccff'],
    confused: ['#cc66ff', '#442266', '#dd88ff'],
    dizzy: ['#cc66ff', '#442266', '#dd88ff'],
  };

  function rr(ctx, x, y, w, h, r, fill, stroke, lw = 2) {
    ctx.beginPath();
    ctx.roundRect(x, y, w, h, r);
    if (fill) { ctx.fillStyle = fill; ctx.fill(); }
    if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = lw; ctx.stroke(); }
  }

  function drawVisor(ctx) {
    rr(ctx, 36, 32, 248, 72, 36, VISOR, INK, 1.5);
    rr(ctx, 44, 38, 232, 60, 30, VISOR_DEEP, null);
    for (let y = 40; y < 96; y += 4) {
      ctx.fillStyle = 'rgba(0,40,60,0.35)';
      ctx.fillRect(44, y, 232, 1);
    }
    rr(ctx, 36, 32, 248, 72, 36, null, INK_BRIGHT, 1);
  }

  function drawBrows(ctx, style, dy = 0) {
    ctx.strokeStyle = INK;
    ctx.lineWidth = 2.5;
    const pairs = [[62, 136], [184, 258]];
    for (const [a, b] of pairs) {
      ctx.beginPath();
      if (style === 'angry') {
        ctx.moveTo(a, 48 + dy); ctx.lineTo(b, b < 200 ? 58 + dy : 48 + dy);
      } else if (style === 'sad') {
        ctx.moveTo(a, 58 + dy); ctx.lineTo(b, b < 200 ? 46 + dy : 58 + dy);
      } else if (style === 'raise') {
        ctx.moveTo(a - 4, 34 + dy); ctx.lineTo(b + 4, 34 + dy);
      } else if (style === 'arch') {
        const cx = (a + b) / 2;
        ctx.moveTo(a, 46 + dy);
        ctx.quadraticCurveTo(cx, 38 + dy, b, 46 + dy);
      } else if (style === 'asym') {
        if (a < 150) { ctx.moveTo(a, 48 + dy); ctx.lineTo(b, 40 + dy); }
        else { ctx.moveTo(a, 58 + dy); ctx.lineTo(b, 50 + dy); }
      } else {
        ctx.moveTo(a, 46 + dy); ctx.lineTo(b, 46 + dy);
      }
      ctx.stroke();
    }
  }

  function drawLid(ctx, cx, cy, r, top, bot, ang) {
    ctx.fillStyle = VISOR;
    if (ang === 'angry-in') {
      const inner = cx > CXC;
      ctx.beginPath();
      if (!inner) {
        ctx.moveTo(cx - r, cy - r); ctx.lineTo(cx + r + 2, cy - r);
        ctx.lineTo(cx + r + 2, cy - r + r * 0.55); ctx.lineTo(cx - r, cy - r + r * 0.2);
      } else {
        ctx.moveTo(cx + r, cy - r); ctx.lineTo(cx - r - 2, cy - r);
        ctx.lineTo(cx - r - 2, cy - r + r * 0.55); ctx.lineTo(cx + r, cy - r + r * 0.2);
      }
      ctx.closePath(); ctx.fill();
    } else if (ang === 'sad-out') {
      const outer = cx < CXC;
      ctx.beginPath();
      if (outer) {
        ctx.moveTo(cx - r, cy + r); ctx.lineTo(cx + r, cy + r);
        ctx.lineTo(cx + r, cy + r - r * 0.5); ctx.lineTo(cx - r, cy + r - r * 0.15);
      } else {
        ctx.moveTo(cx + r, cy + r); ctx.lineTo(cx - r, cy + r);
        ctx.lineTo(cx - r, cy + r - r * 0.5); ctx.lineTo(cx + r, cy + r - r * 0.15);
      }
      ctx.closePath(); ctx.fill();
    }
    if (top > 0) ctx.fillRect(cx - r - 1, cy - r - 1, r * 2 + 2, r * 2 * top + 1);
    if (bot > 0) {
      const h = r * 2 * bot;
      ctx.fillRect(cx - r - 1, cy + r - h, r * 2 + 2, h + 1);
    }
  }

  function drawEye(ctx, side, p, gx, gy, t, blink, bored) {
    const cx = side === 'left' ? EYE_L : EYE_R;
    const cy = EYE_Y;
    const r = p.eyeR || EYE_RAD;
    const wink = (p.winkRight && side === 'right') || (p.winkLeft && side === 'left');

    if (wink) {
      ctx.fillStyle = EYE_GLOW; ctx.beginPath(); ctx.arc(cx, cy, r + 4, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = EYE; ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = INK; ctx.lineWidth = 4;
      ctx.beginPath(); ctx.moveTo(cx - r + 8, cy + 2);
      ctx.quadraticCurveTo(cx, cy + r * 0.35, cx + r - 8, cy + 2); ctx.stroke();
      return;
    }

    ctx.fillStyle = EYE_GLOW; ctx.beginPath(); ctx.arc(cx, cy, r + 5, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = EYE; ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.fill();

    let top = side === 'left' ? (p.topLidL ?? p.topLid ?? 0) : (p.topLidR ?? p.topLid ?? 0);
    let bot = p.botLid || 0;
    top = Math.max(top, blink * 0.9);
    if (bored) top = Math.max(top, 0.45);
    drawLid(ctx, cx, cy, r, top, bot, p.lidAng);

    const px = cx + gx + (p.pupilOffX || 0);
    const py = cy + gy + (p.pupilOffY || 0);
    if (p.pupil === 'heart') {
      ctx.fillStyle = RED;
      const s = 8;
      ctx.beginPath(); ctx.arc(px - s / 3, py - s / 6, s / 2, 0, Math.PI * 2); ctx.fill();
      ctx.beginPath(); ctx.arc(px + s / 3, py - s / 6, s / 2, 0, Math.PI * 2); ctx.fill();
      ctx.beginPath(); ctx.moveTo(px - s, py); ctx.lineTo(px + s, py); ctx.lineTo(px, py + s); ctx.fill();
    } else if (p.pupil === 'spiral') {
      ctx.strokeStyle = PURPLE; ctx.lineWidth = 2;
      ctx.beginPath();
      for (let i = 0; i <= 40; i++) {
        const a = i / 40 * 2.2 * Math.PI * 2 + t * 0.004;
        const rad = 2 + i * 0.35;
        const x = px + Math.cos(a) * rad, y = py + Math.sin(a) * rad;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    } else {
      const s = p.pupilS || 6;
      ctx.fillStyle = PUP; ctx.fillRect(px - s, py - s, s * 2, s * 2);
      ctx.fillStyle = HILITE; ctx.beginPath(); ctx.arc(px - s / 3, py - s / 3, s / 3, 0, Math.PI * 2); ctx.fill();
    }
  }

  function drawMouthGrid(ctx, emotion, t, speaking) {
    const p = PRESETS[emotion] || PRESETS.neutral;
    const m = p.mouth || { x0: 88, x1: 232, gap: 19, teeth: 4, bow: 0 };
    const x0 = m.x0 || 88, x1 = m.x1 || 232, my = 154;
    const w = x1 - x0;
    const ink = MOUTH_COLORS[emotion] ? MOUTH_COLORS[emotion][0] : INK;
    ctx.strokeStyle = ink;
    ctx.lineWidth = 2;
    let pT = 0, pB = 0;
    for (let x = x0; x <= x1; x++) {
      const u = (x - x0) / w;
      const c = 1 - (2 * u - 1) ** 2;
      const half = (m.gap || 19) * 0.5 * (0.5 + 0.5 * Math.sqrt(Math.max(0, c)));
      const bow = (m.bow || 0) * c * 0.08;
      const top = my - half + bow;
      const bot = my + half + bow;
      if (x === x0 || x === x1) {
        ctx.beginPath(); ctx.moveTo(x, top); ctx.lineTo(x, bot); ctx.stroke();
      } else {
        ctx.beginPath(); ctx.moveTo(x - 1, pT); ctx.lineTo(x, top); ctx.stroke();
        ctx.beginPath(); ctx.moveTo(x - 1, pB); ctx.lineTo(x, bot); ctx.stroke();
      }
      pT = top; pB = bot;
    }
    const teeth = m.teeth || 4;
    for (let i = 1; i < teeth; i++) {
      const x = x0 + (w * i) / teeth;
      const u = (x - x0) / w;
      const c = 1 - (2 * u - 1) ** 2;
      const half = (m.gap || 19) * 0.5 * (0.5 + 0.5 * Math.sqrt(Math.max(0, c)));
      const bow = (m.bow || 0) * c * 0.08;
      ctx.beginPath(); ctx.moveTo(x, my - half + bow); ctx.lineTo(x, my + half + bow); ctx.stroke();
    }
  }

  function drawHappyClosedEye(ctx, cx, cy, r, squint = 0, ampLift = 0) {
    ctx.strokeStyle = INK;
    ctx.lineWidth = 2;
    const arcH = 0.34 * (1 - squint * 0.58);
    const arcW = 0.82 * (1 - squint * 0.06);
    const yBase = 6 - (ampLift * 4 + squint * 3);
    for (let w = 0; w < 2; w++) {
      ctx.beginPath();
      for (let i = 0; i <= 14; i++) {
        const u = i / 14;
        const a = Math.PI * (0.12 + u * 0.76);
        const x = cx + Math.cos(a) * r * arcW;
        const y = cy + Math.sin(a) * r * arcH + yBase + w;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
  }

  const VIB_MOUTH_X0 = 70, VIB_MOUTH_X1 = 250, VIB_MOUTH_Y0 = 150, VIB_MOUTH_Y1 = 186;
  const vibingNotes = Array.from({ length: 4 }, () => ({ x: 0, y: -50, vx: 0, vy: 0, kind: 0 }));
  let lastNoteSpawn = 0;

  function drawMusicNote(ctx, x, y, headR, flagged) {
    ctx.fillStyle = INK;
    ctx.beginPath(); ctx.arc(x, y, headR, 0, Math.PI * 2); ctx.fill();
    const stemH = headR * 4;
    ctx.fillRect(x + headR - 1, y - stemH + headR, 2, stemH);
    if (flagged) {
      ctx.beginPath();
      ctx.moveTo(x + headR, y - stemH + headR);
      ctx.lineTo(x + headR + headR * 2, y - stemH + headR + headR);
      ctx.lineTo(x + headR, y - stemH + headR + headR * 2);
      ctx.fill();
    }
  }

  function vibingHighTone(bands) {
    let peak = 0, sum = 0;
    for (let b = 7; b < bands.length; b++) {
      sum += bands[b];
      if (bands[b] > peak) peak = bands[b];
    }
    const avg = sum / (bands.length - 7);
    return Math.min(1, (peak * 0.72 + avg * 0.28) / 220);
  }

  function tickVibingNotes(t, amp, hi, bob) {
    const noteDrive = Math.max(amp * 0.22, hi);
    if (noteDrive < 0.08) return;
    const spawnMs = Math.max(70, 410 - noteDrive * 300);
    if (t - lastNoteSpawn > spawnMs) {
      lastNoteSpawn = t;
      const spawns = hi > 0.52 ? 2 : 1;
      for (let s = 0; s < spawns; s++) {
        const slot = vibingNotes.find(n => n.y < -30);
        if (slot) {
          slot.x = 118 + Math.random() * 85;
          slot.y = VIB_MOUTH_Y0 - 18 + bob + Math.random() * 12;
          slot.vy = -1.5 - noteDrive * 2.9 - Math.random() * 0.9;
          slot.vx = (Math.random() < 0.5 ? -1 : 1) * (0.5 + Math.random() * 0.7);
          slot.kind = (hi > 0.38 && Math.random() < 0.67) ? 1 : (Math.random() < 0.5 ? 0 : 1);
        }
      }
    }
    for (const n of vibingNotes) {
      if (n.y < -35 || n.y > 210) continue;
      n.x += n.vx;
      n.y += n.vy;
    }
  }

  function drawVibingNotes(ctx, amp, hi) {
    const noteDrive = Math.max(amp * 0.22, hi);
    for (const n of vibingNotes) {
      if (n.y < -35 || n.y > 210) continue;
      const headR = Math.min(7, 4 + noteDrive * 3.5);
      drawMusicNote(ctx, n.x, n.y, headR, n.kind !== 0);
    }
  }

  const vibingColHist = new Uint8Array(20);

  function advanceVibingSpec(bands) {
    vibingColHist.copyWithin(0, 1);
    let peak = 0, sum = 0;
    for (let b = 0; b < bands.length; b++) {
      sum += bands[b];
      if (bands[b] > peak) peak = bands[b];
    }
    const avg = sum / bands.length;
    vibingColHist[vibingColHist.length - 1] = Math.min(220, Math.floor(peak * 0.62 + avg * 0.38));
  }

  function fakeVibingBands(t) {
    const amp = vibingAmp(t);
    const out = new Array(12).fill(0);
    for (let i = 0; i < 12; i++) {
      const hiBoost = i >= 7 ? 1.1 + 0.45 * Math.abs(Math.sin(t * 0.021 + i * 0.7)) : 1.0;
      const w = 0.35 + 0.65 * Math.abs(Math.sin(t * 0.014 + i * 0.55));
      const beat = 0.5 + 0.5 * Math.sin(t * 0.009 + i * 0.2);
      out[i] = Math.min(220, Math.floor(amp * 220 * w * beat * hiBoost));
    }
    return out;
  }

  function vibingBarColors(cx, bars, t, time) {
    const u = cx / (bars - 1);
    const pulse = 0.82 + 0.18 * Math.sin(time * 0.011 + cx * 0.42);
    const e = t * pulse;
    let base;
    if (u < 0.33) base = lerpColor('#006688', INK, u / 0.33);
    else if (u < 0.66) base = lerpColor(INK, '#44ff88', (u - 0.33) / 0.33);
    else base = lerpColor('#44ff88', '#ff44cc', (u - 0.66) / 0.34);
    const glow = lerpColor(VISOR_DEEP, base, 0.30 + e * 0.55);
    const bot = lerpColor(base, INK_BRIGHT, e * 0.72);
    const top = lerpColor(base, VISOR_DEEP, 0.28 + (1 - e) * 0.22);
    const hi = lerpColor(INK_BRIGHT, '#ffffff', e * 0.65);
    return { glow, bot, top, hi };
  }

  function lerpColor(a, b, t) {
    const pa = parseInt(a.slice(1), 16);
    const pb = parseInt(b.slice(1), 16);
    const r = (pa >> 16 & 0xff) + ((pb >> 16 & 0xff) - (pa >> 16 & 0xff)) * t;
    const g = (pa >> 8 & 0xff) + ((pb >> 8 & 0xff) - (pa >> 8 & 0xff)) * t;
    const bl = (pa & 0xff) + ((pb & 0xff) - (pa & 0xff)) * t;
    return `rgb(${r | 0},${g | 0},${bl | 0})`;
  }

  function drawVibingMirrorBar(ctx, cx, bars, sx, barW, midY, halfH, t, time) {
    if (halfH < 2) halfH = 2;
    const totalH = halfH * 2;
    const { glow, bot, top, hi } = vibingBarColors(cx, bars, t, time);
    rr(ctx, sx - 1, midY - halfH - 1, barW + 2, totalH + 2, 3, glow, null);
    rr(ctx, sx, midY - halfH, barW, halfH, 2, top, null);
    rr(ctx, sx, midY, barW, halfH, 2, bot, null);
    if (t > 0.28) {
      ctx.fillStyle = hi;
      ctx.fillRect(sx + 1, midY, barW - 2, 1);
    }
    if (t > 0.62 && halfH > 3) {
      ctx.fillRect(sx + 1, midY - halfH + 1, barW - 2, 1);
    }
  }

  function drawVibingSpectrogramMouth(ctx, bob, time) {
    const x0 = VIB_MOUTH_X0, x1 = VIB_MOUTH_X1, y0 = VIB_MOUTH_Y0 + bob, y1 = VIB_MOUTH_Y1 + bob;
    const pad = 5, gap = 2;
    const cols = MOUTH_COLORS.vibing || [INK, INK_GLOW, INK_BRIGHT];
    const [segInk] = cols;
    rr(ctx, x0, y0, x1 - x0, y1 - y0, 10, VISOR_DEEP, segInk, 1);
    const innerW = x1 - x0 - pad * 2;
    const innerH = y1 - y0 - pad * 2;
    const bars = vibingColHist.length;
    const barW = (innerW - gap * (bars - 1)) / bars;
    const gridW = barW * bars + gap * (bars - 1);
    const mouthCx = (x0 + x1) / 2;
    const ox = mouthCx - gridW / 2;
    const oy = y0 + pad;
    const midY = oy + Math.floor(innerH / 2);
    const maxHalf = Math.floor(innerH / 2);
    for (let cx = 0; cx < bars; cx++) {
      const v = vibingColHist[cx];
      if (v >= 6) {
        const t = Math.min(1, Math.pow(v / 220, 0.68));
        const halfH = Math.max(2, Math.floor(maxHalf * t));
        const sx = ox + cx * (barW + gap);
        drawVibingMirrorBar(ctx, cx, bars, sx, barW, midY, halfH, t, time);
      }
    }
    rr(ctx, x0, y0, x1 - x0, y1 - y0, 10, null, segInk, 1);
  }

  function vibingAmp(t) {
    const mic = 0.15 + 0.85 * Math.abs(Math.sin(t * 0.02)) * (0.4 + 0.6 * Math.abs(Math.sin(t * 0.047)));
    const idle = 0.12 + 0.10 * Math.sin(t * 0.0038);
    return Math.max(mic, idle);
  }

  function vibingLidBoundaryY(cx, cy, r, drop, arch, x) {
    const nx = (x - cx) / r;
    return (cy - r) + 2 * r * drop + arch * r * (1 - nx * nx);
  }

  function drawVibingRelaxedEye(ctx, cx, cy, r, lidPulse, gazeX) {
    ctx.fillStyle = VISOR_DEEP;
    ctx.beginPath(); ctx.arc(cx, cy, r + 4, 0, Math.PI * 2); ctx.fill();

    const drop = 0.40 + lidPulse * 0.06;
    const arch = 0.26 + lidPulse * 0.06;

    for (let y = cy - r; y <= cy + r; y++) {
      const disc = r * r - (y - cy) * (y - cy);
      if (disc <= 0) continue;
      const halfW = Math.floor(Math.sqrt(disc));
      const xL = cx - halfW;
      const xR = cx + halfW;
      let x = xL;
      while (x <= xR) {
        const bnd = vibingLidBoundaryY(cx, cy, r, drop, arch, x);
        if (y + 0.5 < bnd) {
          const rs = x;
          while (x <= xR && y + 0.5 < vibingLidBoundaryY(cx, cy, r, drop, arch, x)) x++;
          ctx.fillStyle = VISOR_DEEP;
          ctx.fillRect(rs, y, x - rs, 1);
        } else {
          const rs = x;
          while (x <= xR && y + 0.5 >= vibingLidBoundaryY(cx, cy, r, drop, arch, x)) x++;
          ctx.fillStyle = EYE;
          ctx.fillRect(rs, y, x - rs, 1);
        }
      }
    }

    ctx.strokeStyle = EYE_GLOW;
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.arc(cx, cy, r + 1, 0, Math.PI * 2); ctx.stroke();

    const px = cx + gazeX;
    const py = cy + 5;
    const s = 5;
    ctx.fillStyle = PUP;
    ctx.fillRect(px - s, py - s, s * 2, s * 2);
    ctx.fillStyle = HILITE;
    ctx.beginPath(); ctx.arc(px - s / 3, py - s / 3, s / 3, 0, Math.PI * 2); ctx.fill();
  }

  function drawVibingFace(ctx, t) {
    const amp = vibingAmp(t);
    const beatHz = 1.6 + amp * 1.4;
    const bobHz = beatHz * 0.68;
    const targetBob = Math.sin(t * 0.001 * bobHz * Math.PI * 2) * (0.5 + amp * 1.6);
    if (drawVibingFace._bob === undefined) drawVibingFace._bob = 0;
    drawVibingFace._bob += (targetBob - drawVibingFace._bob) * 0.09;
    const bob = drawVibingFace._bob;
    const bands = fakeVibingBands(t);
    advanceVibingSpec(bands);
    advanceVibingSpec(fakeVibingBands(t));
    const hi = vibingHighTone(bands);
    tickVibingNotes(t, amp, hi, bob);

    const beat = Math.sin(t * 0.001 * beatHz * Math.PI * 2);
    const lidTarget = 0.5 + 0.5 * beat;
    if (drawVibingFace._lidPulse === undefined) drawVibingFace._lidPulse = 0.5;
    drawVibingFace._lidPulse += (lidTarget - drawVibingFace._lidPulse) * 0.12;

    const gazeAmpX = 2.2 + amp * 3.0;
    const targetGx = Math.sin(t * 0.001 * beatHz * Math.PI * 2) * gazeAmpX;
    if (drawVibingFace._gx === undefined) drawVibingFace._gx = 0;
    if (drawVibingFace._gy === undefined) drawVibingFace._gy = 0;
    drawVibingFace._gx += (targetGx - drawVibingFace._gx) * 0.08;
    drawVibingFace._gy += (0 - drawVibingFace._gy) * 0.12;

    const browDy = Math.sin(t * 0.001 * bobHz * Math.PI * 2) * (0.4 + amp * 0.9);
    ctx.fillStyle = BG;
    ctx.fillRect(0, 0, W, H);
    ctx.save();
    ctx.translate(0, bob);
    drawVisor(ctx);
    drawVibingRelaxedEye(ctx, EYE_L, EYE_Y, EYE_RAD, drawVibingFace._lidPulse, drawVibingFace._gx);
    drawVibingRelaxedEye(ctx, EYE_R, EYE_Y, EYE_RAD, drawVibingFace._lidPulse, drawVibingFace._gx);
    drawVibingSpectrogramMouth(ctx, 0, t);
    ctx.restore();
    drawBrows(ctx, 'arch', browDy);
    drawVibingNotes(ctx, amp, hi);
  }

  function drawLedMouth(ctx, emotion, t, speaking) {
    const p = PRESETS[emotion] || PRESETS.neutral;
    const x0 = 78, x1 = 242, y0 = 138, y1 = 172;
    const segs = p.mouthSegs || 7;
    const cols = MOUTH_COLORS[emotion] || [INK, INK_GLOW, INK_BRIGHT];
    const [segInk, segGlow, segHi] = cols;
    const pad = 5, gap = 3;
    rr(ctx, x0, y0, x1 - x0, y1 - y0, 10, VISOR_DEEP, segInk, 1);
    const troughH = y1 - y0 - pad * 2;
    const innerW = x1 - x0 - pad * 2;
    const segW = (innerW - gap * (segs - 1)) / segs;
    let sx = x0 + pad;
    const bow = p.mouthBow || 0;
    const speakAnim = speaking;
    const amp = speakAnim ? 0.35 + 0.65 * Math.abs(Math.sin(t * 0.012)) : 0;
    for (let i = 0; i < segs; i++) {
      const u = i / (segs - 1);
      const env = Math.sin(u * Math.PI) ** 2;
      let sh = speakAnim
        ? troughH * (0.35 + 0.65 * amp * env)
        : troughH * (0.5 + 0.5 * env + bow * Math.sin(u * Math.PI) * 0.35);
      sh = Math.max(5, Math.min(troughH, sh));
      const sy = y0 + pad + (troughH - sh) / 2;
      rr(ctx, sx - 1, sy - 1, segW + 2, sh + 2, 4, segGlow, null);
      rr(ctx, sx, sy, segW, sh, 3, segInk, null);
      if (!speakAnim && i % 2 === 0) {
        ctx.fillStyle = segHi;
        ctx.fillRect(sx + 1, sy + 1, segW - 2, 1);
      }
      sx += segW + gap;
    }
  }

  function animState(emotion, t) {
    const blink = Math.max(0, Math.sin(t * 0.008) > 0.92 ? (Math.sin(t * 0.08) + 1) * 0.5 : 0);
    let gazeX = Math.sin(t * 0.006) * 5, gazeY = 0;
    if (emotion === 'thinking') {
      gazeX = Math.sin(t * 0.02) > 0 ? 10 : -10;
      gazeY = -3;
    } else if (emotion === 'vibing') {
      gazeX = Math.sin(t * 0.0036) * 5;
      gazeY = Math.sin(t * 0.0027) * 2.5;
    }
    const vAmp = emotion === 'vibing'
      ? Math.pow(0.15 + 0.85 * Math.abs(Math.sin(t * 0.018)) * (0.35 + 0.65 * Math.abs(Math.sin(t * 0.041))), 0.48)
      : 0;
    return { blink, gazeX, gazeY, amp: vAmp };
  }

  function drawFace(ctx, emotion, t, opts = {}) {
    if (emotion === 'vibing') {
      drawVibingFace(ctx, t);
      return;
    }
    const p = { ...(PRESETS[emotion] || PRESETS.neutral) };
    const { speaking = false, bored = false } = opts;
    const anim = animState(emotion, t);
    ctx.fillStyle = BG;
    ctx.fillRect(0, 0, W, H);
    drawVisor(ctx);
    drawBrows(ctx, p.brow || 'flat', Math.sin(t * 0.004) * 0.8);
    drawEye(ctx, 'left', p, anim.gazeX, anim.gazeY, t, anim.blink, bored);
    drawEye(ctx, 'right', p, anim.gazeX, anim.gazeY, t, anim.blink, bored);
    if (p.tear) {
      ctx.fillStyle = BLUE;
      const tx = EYE_L - 6, ty = EYE_Y + (p.eyeR || EYE_RAD) + 4;
      ctx.beginPath(); ctx.arc(tx, ty + 12, 6, 0, Math.PI * 2); ctx.fill();
    }
    if (!speaking && (emotion === 'angry' || emotion === 'neutral' || emotion === 'thinking' || emotion === 'confused')) {
      drawMouthGrid(ctx, emotion, t, speaking);
    } else {
      drawLedMouth(ctx, emotion, t, speaking);
    }
  }

  function attachPreview(canvas, getOpts) {
    let emotion = 'neutral';
    let t = 0;
    let raf = 0;
    const ctx = canvas.getContext('2d');
    const loop = () => {
      t += 16;
      const opts = typeof getOpts === 'function' ? getOpts() : {};
      drawFace(ctx, emotion, t, opts);
      raf = requestAnimationFrame(loop);
    };
    return {
      setEmotion(e) { emotion = e || 'neutral'; },
      start() { if (!raf) loop(); },
      stop() { cancelAnimationFrame(raf); raf = 0; },
    };
  }

  global.FacePreview = { W, H, EMOTIONS, drawFace, attachPreview };
})(typeof window !== 'undefined' ? window : globalThis);
