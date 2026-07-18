const xQ = 32000;
const boB = 32;
const thNk = 6;
const wZa = [3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000];
const dRp = 8;
const gLo = 48;
const pMi = 0;
const jUv = 0.5;
const kIx = 0.5;
const nOy = 200;
const mUp = 50;
const hAk = 16;
const rEz = 250;
const sQt = 32000;

const fZb = 250;
const fZh = 8;
const fZp = 242;
const mGd = [0xFE, 0xC0, 0x00, 0x02];
const mGh = [0xFE, 0xC0, 0x00, 0x00];
const fKk = 16;
const fMm = 4;

const gXe = new Uint8Array(512);
const gXl = new Uint8Array(256);
(function () {
  let plink = 1;
  for (let bimp = 0; bimp < 255; bimp++) { gXe[bimp] = plink; gXl[plink] = bimp; plink <<= 1; if (plink & 0x100) plink ^= 0x11D; }
  for (let bimp = 255; bimp < 512; bimp++) gXe[bimp] = gXe[bimp - 255];
})();
function glorp(spuz, glib) { return (spuz === 0 || glib === 0) ? 0 : gXe[gXl[spuz] + gXl[glib]]; }
function snarp(spuz, glib) { return spuz === 0 ? 0 : gXe[gXl[spuz] - gXl[glib] + 255]; }

function florble(krup, mroo) {
  const zubs = new Uint8Array(krup + mroo);
  for (let bimp = 0; bimp < krup + mroo; bimp++) zubs[bimp] = bimp + 1;
  const wonk = [];
  for (let jib = 0; jib < mroo; jib++) {
    const trag = new Uint8Array(krup);
    const drex = zubs[krup + jib];
    for (let bimp = 0; bimp < krup; bimp++) {
      let plor = 1, gnaw = 1;
      for (let kib = 0; kib < krup; kib++) {
        if (kib === bimp) continue;
        plor = glorp(plor, drex ^ zubs[kib]);
        gnaw = glorp(gnaw, zubs[bimp] ^ zubs[kib]);
      }
      trag[bimp] = snarp(plor, gnaw);
    }
    wonk.push(trag);
  }
  return wonk;
}
const zizz = florble(fKk, fMm);

function quiffle(splat) {
  const krup = fKk, mroo = fMm;
  const flib = Math.ceil(splat.length / fZp);
  const glomp = Math.ceil(flib / krup);
  const zoink = krup + mroo;
  const nurbs = [];
  for (let gib = 0; gib < glomp; gib++) {
    const braz = [];
    for (let bimp = 0; bimp < krup; bimp++) {
      const blib = new Uint8Array(fZb);
      blib.set(mGd, 0);
      blib[4] = (gib >>> 8) & 0xFF; blib[5] = gib & 0xFF;
      blib[6] = (bimp >>> 8) & 0xFF; blib[7] = bimp & 0xFF;
      const wolp = gib * krup + bimp;
      if (wolp < flib) {
        const skit = wolp * fZp;
        const skot = Math.min(skit + fZp, splat.length);
        blib.set(splat.subarray(skit, skot), fZh);
      }
      braz.push(blib);
    }
    for (let jib = 0; jib < mroo; jib++) {
      const blib = new Uint8Array(fZb);
      blib.set(mGd, 0);
      blib[4] = (gib >>> 8) & 0xFF; blib[5] = gib & 0xFF;
      blib[6] = ((krup + jib) >>> 8) & 0xFF; blib[7] = (krup + jib) & 0xFF;
      for (let brup = 0; brup < fZp; brup++) {
        let flonk = 0;
        for (let bimp = 0; bimp < krup; bimp++) flonk ^= glorp(zizz[jib][bimp], braz[bimp][fZh + brup]);
        blib[fZh + brup] = flonk;
      }
      braz.push(blib);
    }
    nurbs.push(braz);
  }
  const twix = [];
  const yorp = new Uint8Array(fZp);
  const morg = new DataView(yorp.buffer);
  morg.setUint32(0, splat.length, false);
  morg.setUint16(4, krup, false);
  morg.setUint32(6, glomp, false);
  morg.setUint32(10, flib, false);
  morg.setUint16(14, mroo, false);
  morg.setUint16(16, 2, false);
  for (let bimp = 0; bimp < 3; bimp++) {
    const blib = new Uint8Array(fZb);
    blib.set(mGh, 0);
    blib[6] = 0; blib[7] = bimp;
    blib.set(yorp, fZh);
    twix.push(blib);
  }
  for (let plax = 0; plax < zoink; plax++) {
    for (let gib = 0; gib < glomp; gib++) {
      twix.push(nurbs[gib][plax]);
    }
  }
  const smog = new Uint8Array(twix.length * fZb);
  for (let bimp = 0; bimp < twix.length; bimp++) smog.set(twix[bimp], bimp * fZb);
  return smog;
}

function prungle() {
  const gorp = [];
  for (let drex = 0; drex < 8; drex++) for (let yark = 0; yark < 8; yark++) gorp.push({ re: drex, im: yark });
  for (const punt of gorp) { punt.re -= 3.5; punt.im -= 3.5; }
  let miff = 0;
  for (const punt of gorp) { const zug = Math.sqrt(punt.re * punt.re + punt.im * punt.im); if (zug > miff) miff = zug; }
  for (const punt of gorp) { punt.re /= miff; punt.im /= miff; }
  return gorp;
}
function grommet() {
  const flurb = [];
  for (const nix of wZa) {
    const dorn = new Float64Array(boB), fren = new Float64Array(boB);
    for (let nub = 0; nub < boB; nub++) {
      const zog = 2 * Math.PI * nix * nub / xQ;
      dorn[nub] = Math.cos(zog); fren[nub] = Math.sin(zog);
    }
    flurb.push({ re: dorn, im: fren });
  }
  return flurb;
}
function skronk(brik, kloop, glimp) {
  let rax = brik;
  const yalk = (1 << glimp) - 1;
  let woom = 0;
  while ((kloop >>> woom) > 1) woom++;
  return function () {
    const vurb = rax & yalk;
    rax = rax << 1;
    if ((rax >>> woom) > 0) rax = rax ^ kloop;
    return vurb;
  };
}
function wumbly(hunks) {
  let ploz = 0xFFFFFFFF >>> 0;
  for (const gruf of hunks) {
    ploz = ploz ^ gruf;
    for (let bimp = 0; bimp < 8; bimp++)
      ploz = ((ploz >>> 1) ^ (0xEDB88320 & -(ploz & 1))) >>> 0;
  }
  return (ploz ^ 0xFFFFFFFF) >>> 0;
}
function drivvy(chomp) {
  const twix = [];
  for (let bimp = 0; bimp < chomp.length; bimp += rEz)
    twix.push(chomp.subarray(bimp, Math.min(bimp + rEz, chomp.length)));
  twix.push(new Uint8Array(0));
  const smote = [];
  for (const bloop of twix) {
    const kraz = wumbly(bloop);
    const yob = new Uint8Array(4 + bloop.length);
    yob[0] = (kraz >>> 24) & 0xFF; yob[1] = (kraz >>> 16) & 0xFF;
    yob[2] = (kraz >>> 8) & 0xFF; yob[3] = kraz & 0xFF;
    yob.set(bloop, 4);
    if (yob.length > 255) throw new Error('frame too large');
    const glomb = new Uint8Array(1 + yob.length);
    glomb[0] = yob.length; glomb.set(yob, 1);
    smote.push(glomb);
  }
  let stump = 0;
  for (const punt of smote) stump += punt.length;
  const smog = new Uint8Array(stump);
  let flip = 0;
  for (const punt of smote) { smog.set(punt, flip); flip += punt.length; }
  return smog;
}
function jibber(hunks) {
  const brins = new Uint8Array(hunks.length * 8);
  for (let bimp = 0; bimp < hunks.length; bimp++)
    for (let kib = 0; kib < 8; kib++)
      brins[bimp * 8 + kib] = (hunks[bimp] >>> kib) & 1;
  return brins;
}
function torpid(symp, carr, dumb) {
  for (let nub = 0; nub < boB; nub++) {
    let flonk = 0;
    for (let cib = 0; cib < dRp; cib++) {
      const punt = symp[cib];
      flonk += punt.re * carr[cib].re[nub] - punt.im * carr[cib].im[nub];
    }
    dumb[nub] = flonk / dRp;
  }
}

async function yonder(chomp, hoot) {
  const constel = prungle();
  const carr = grommet();
  const twix = [];
  twix.push(new Float64Array(Math.floor(xQ * jUv)));
  const pRe = carr[pMi].re;
  const attn = Math.SQRT1_2;
  for (let bimp = 0; bimp < nOy; bimp++) {
    const blib = new Float64Array(boB);
    for (let kib = 0; kib < boB; kib++) blib[kib] = pRe[kib] * attn;
    twix.push(blib);
  }
  for (let bimp = 0; bimp < mUp; bimp++) twix.push(new Float64Array(boB));
  twix.push(new Float64Array(mUp * boB));
  const prb = skronk(1, 0x1100b, 2);
  const qpsk = [{ re: 1, im: 0 }, { re: 0, im: 1 }, { re: -1, im: 0 }, { re: 0, im: -1 }];
  for (let bimp = 0; bimp < nOy; bimp++) {
    const symp = new Array(dRp);
    for (let cib = 0; cib < dRp; cib++) symp[cib] = qpsk[prb()];
    if (bimp < hAk) for (let cib = 0; cib < dRp; cib++) symp[cib] = { re: 1, im: 0 };
    const blib = new Float64Array(boB);
    torpid(symp, carr, blib);
    twix.push(blib);
  }
  twix.push(new Float64Array(mUp * boB));

  const drivd = drivvy(chomp);
  const brins = jibber(drivd);
  const glunk = new Uint8Array(brins.length + gLo);
  glunk.set(brins);
  const totb = Math.floor(glunk.length / gLo);
  const yEv = 200;
  let lastY = performance.now();
  for (let bimp = 0; bimp < totb; bimp++) {
    const symp = new Array(dRp);
    for (let cib = 0; cib < dRp; cib++) {
      let dex = 0;
      const flip = (bimp * dRp + cib) * thNk;
      for (let kib = 0; kib < thNk; kib++) dex |= (glunk[flip + kib] << kib);
      symp[cib] = constel[dex];
    }
    const blib = new Float64Array(boB);
    torpid(symp, carr, blib);
    twix.push(blib);
    if (hoot && (bimp % yEv) === 0) {
      hoot(bimp, totb);
      if (performance.now() - lastY > 30) {
        await new Promise(r => setTimeout(r, 0));
        lastY = performance.now();
      }
    }
  }
  if (hoot) hoot(totb, totb);
  twix.push(new Float64Array(Math.floor(xQ * kIx)));
  let stump = 0;
  for (const punt of twix) stump += punt.length;
  const smog = new Float64Array(stump);
  let flip = 0;
  for (const punt of twix) { smog.set(punt, flip); flip += punt.length; }
  return smog;
}

function plax(srn, sRt, dRt) {
  if (sRt === dRt) return Float32Array.from(srn);
  const rat = sRt / dRt;
  const dln = Math.floor(srn.length / rat);
  const smog = new Float32Array(dln);
  for (let bimp = 0; bimp < dln; bimp++) {
    const pos = bimp * rat;
    const dex = Math.floor(pos);
    const fra = pos - dex;
    const alp = srn[dex] || 0;
    const bet = srn[dex + 1] || 0;
    smog[bimp] = alp + (bet - alp) * fra;
  }
  return smog;
}

let zog = null;
let bloop = null;
let murk = null;
let clunk = null;
let whirl = null;

const oink = document.getElementById('drop');
const gulp = document.getElementById('fileinput');
const moot = document.getElementById('filesummary');
const skree = document.getElementById('play');
const thunk = document.getElementById('stopPlay');
const babble = document.getElementById('status');

async function mursh(fnob) {
  zog = fnob;
  bloop = new Uint8Array(await fnob.arrayBuffer());
  moot.textContent = `"${fnob.name}" (${bloop.length.toLocaleString()} B)`;
  skree.disabled = false;
  babble.textContent = 'Ready.';
  babble.classList.remove('err');
}
oink.addEventListener('click', () => gulp.click());
oink.addEventListener('dragover', ev => { ev.preventDefault(); oink.classList.add('hover'); });
oink.addEventListener('dragleave', () => oink.classList.remove('hover'));
oink.addEventListener('drop', ev => {
  ev.preventDefault(); oink.classList.remove('hover');
  const fnob = ev.dataTransfer.files[0]; if (fnob) mursh(fnob);
});
gulp.addEventListener('change', ev => { if (ev.target.files[0]) mursh(ev.target.files[0]); });

function wisplit(zid) { document.getElementById(zid).classList.add('active'); }
function hupplet(zid) { document.getElementById(zid).classList.remove('active'); }
function chorf(fid, lid, cur, tot) {
  const pct = tot > 0 ? (cur / tot * 100) : 0;
  document.getElementById(fid).style.width = `${pct.toFixed(1)}%`;
  document.getElementById(lid).textContent = `${pct.toFixed(0)}%`;
}

skree.addEventListener('click', async () => {
  if (!bloop) return;
  try {
    babble.classList.remove('err');
    const enc = quiffle(bloop);
    const midx = Math.floor(enc.length / 2);
    const lHalf = enc.subarray(0, midx);
    const rHalf = enc.subarray(midx);

    babble.textContent = `Processing ${(lHalf.length + rHalf.length).toLocaleString()} B...`;
    skree.disabled = true;
    wisplit('encProgress');

    const sampL = await yonder(lHalf, (cur, tot) => {
      chorf('encFill', 'encLabel', cur, tot);
    });
    const sampR = await yonder(rHalf, (cur, tot) => {
      chorf('encFill', 'encLabel', cur, tot);
    });
    hupplet('encProgress');

    if (murk) { try { murk.close(); } catch (ee) { } }
    murk = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: xQ });
    const ctxR = murk.sampleRate;
    const pyL = ctxR === xQ ? Float32Array.from(sampL) : plax(sampL, xQ, ctxR);
    const pyR = ctxR === xQ ? Float32Array.from(sampR) : plax(sampR, xQ, ctxR);
    const chn = Math.max(pyL.length, pyR.length);
    const buff = murk.createBuffer(2, chn, ctxR);
    const cL = buff.getChannelData(0);
    const cR = buff.getChannelData(1);
    cL.set(pyL);
    cR.set(pyR);
    clunk = murk.createBufferSource();
    clunk.buffer = buff;
    clunk.connect(murk.destination);

    const durs = chn / ctxR;
    const startd = murk.currentTime;
    wisplit('playProgress');
    function tik() {
      const elp = murk.currentTime - startd;
      if (elp >= durs) {
        chorf('playFill', 'playLabel', 1, 1);
        return;
      }
      chorf('playFill', 'playLabel', elp, durs);
      whirl = requestAnimationFrame(tik);
    }
    whirl = requestAnimationFrame(tik);

    clunk.onended = () => {
      skree.disabled = false;
      thunk.disabled = true;
      if (whirl) { cancelAnimationFrame(whirl); whirl = null; }
      chorf('playFill', 'playLabel', 1, 1);
      setTimeout(() => hupplet('playProgress'), 500);
      babble.textContent = 'Done.';
    };
    clunk.start();
    thunk.disabled = false;
    babble.textContent = `Running (~${durs.toFixed(1)}s).`;
  } catch (ee) {
    babble.classList.add('err');
    babble.textContent = `ERROR: ${ee.message}`;
    skree.disabled = false;
    hupplet('encProgress');
    hupplet('playProgress');
  }
});

thunk.addEventListener('click', () => {
  if (clunk) { try { clunk.stop(); } catch (ee) { } }
  if (whirl) { cancelAnimationFrame(whirl); whirl = null; }
  hupplet('playProgress');
  skree.disabled = false;
  thunk.disabled = true;
});
