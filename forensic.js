// ─── Forensic evidence (data ingested by ./prima prima_forensic.prima) ───
const EVIDENCE = [
  { id:"T01", amount:1420.50, cat:"equipment", status:"cleared"   },
  { id:"T02", amount:89500.00,cat:"materials",status:"flagged"  },
  { id:"T03", amount:45.00,   cat:"office",    status:"cleared"   },
  { id:"T04", amount:3200.75, cat:"equipment", status:"flagged"  },
  { id:"T05", amount:12.99,   cat:"office",    status:"cleared"   },
  { id:"T06", amount:76500.00,cat:"materials",status:"cleared"   },
  { id:"T07", amount:890.00,  cat:"travel",    status:"flagged"  },
  { id:"T08", amount:150.00,  cat:"office",    status:"cleared"   },
  { id:"T09", amount:42000.00,cat:"materials",status:"flagged"  },
  { id:"T10", amount:99.00,   cat:"travel",    status:"cleared"   },
  { id:"T11", amount:23100.50,cat:"equipment", status:"cleared"   },
  { id:"T12", amount:3300.00, cat:"travel",    status:"cleared"   },
  { id:"T13", amount:58000.00,cat:"materials",status:"flagged"  },
  { id:"T14", amount:22.50,   cat:"office",    status:"cleared"   },
  { id:"T15", amount:4100.00, cat:"equipment", status:"cleared"   },
];

// ─── Forensic constraint classification ───
// admitted (as): status cleared AND amount < 25000
// flagged (frontier): amount 25000-60000 OR status flagged
// excluded (not_as): amount >= 60000 (critical anomaly)
const CC = { bg:'#000', blue:'#5a7aff', orange:'#e06a3a', gold:'#f5c451', ink:'#f5f5f7' };

function classify(tx) {
  if (tx.amount >= 60000) return { verdict:'excluded', color:CC.orange };
  if (tx.amount >= 25000 || tx.status === 'flagged') return { verdict:'flagged', color:CC.gold };
  return { verdict:'admitted', color:CC.blue };
}

const classified = EVIDENCE.map(tx => ({ ...tx, ...classify(tx) }));

// ─── KPI update ───
const total = classified.length;
const admitted = classified.filter(t => t.verdict === 'admitted').length;
const excluded = classified.filter(t => t.verdict === 'excluded').length;
const flagged = classified.filter(t => t.verdict === 'flagged').length;

document.getElementById('kTotal').textContent = total;
document.getElementById('kAs').textContent = admitted;
document.getElementById('kNa').textContent = excluded;
document.getElementById('kFr').textContent = flagged;
document.getElementById('recordCount').textContent = total + ' records';

// ─── Findings list ───
const el = document.getElementById('findings');
el.innerHTML = classified.slice().reverse().map(t => `
  <div class="finding" style="border-left:2px solid ${t.color}">
    <div class="id">${t.id}</div>
    <div class="verdict ${t.verdict}">${t.verdict}</div>
    <div class="info">
      <div class="desc">${t.cat}</div>
      <div class="sub">${t.status}</div>
    </div>
    <div class="amount">$${t.amount.toLocaleString('en-US',{maximumFractionDigits:2})}</div>
  </div>
`).join('');

// ─── Evidence map: forensic scatter plot (canvas) ───
const cv = document.getElementById('chart');
const ctx = cv.getContext('2d');
const DPR = Math.min(window.devicePixelRatio || 1, 2);

function resize() {
  const r = cv.parentElement.getBoundingClientRect();
  cv.width = r.width * DPR;
  cv.height = r.height * DPR;
  ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
  draw();
}

function draw() {
  const W = cv.width / DPR, H = cv.height / DPR;
  const M = 40;
  ctx.clearRect(0, 0, W, H);

  // faint grid
  ctx.strokeStyle = 'rgba(255,255,255,0.04)';
  ctx.lineWidth = 1;
  for (let g = 0; g < 6; g++) {
    const y = M + (H - M * 2) / 5 * g;
    ctx.beginPath(); ctx.moveTo(M, y); ctx.lineTo(W - M, y); ctx.stroke();
  }

  // threshold lines
  const h = H - M * 2;
  const base = H - M;
  const yScale = h / 100000;

  // exclusion threshold (60000)
  const yExcl = base - 60000 * yScale;
  ctx.strokeStyle = CC.orange;
  ctx.globalAlpha = 0.3;
  ctx.setLineDash([4, 6]); ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(M, yExcl); ctx.lineTo(W - M, yExcl); ctx.stroke();
  ctx.setLineDash([]);
  // frontier threshold (25000)
  const yFlag = base - 25000 * yScale;
  ctx.strokeStyle = CC.gold;
  ctx.globalAlpha = 0.3;
  ctx.setLineDash([4, 6]);
  ctx.beginPath(); ctx.moveTo(M, yFlag); ctx.lineTo(W - M, yFlag); ctx.stroke();
  ctx.setLineDash([]);
  ctx.globalAlpha = 1;

  // points
  const xGap = (W - M * 2) / (total - 1);
  for (let i = 0; i < total; i++) {
    const t = classified[i];
    const x = M + i * xGap;
    const y = base - t.amount * yScale;
    const r = 3 + (t.amount / 30000) * 4;
    ctx.fillStyle = t.color;
    ctx.globalAlpha = 0.9;
    ctx.beginPath(); ctx.arc(x, y, Math.min(r, 8), 0, 6.2832); ctx.fill();
    ctx.globalAlpha = 1;
  }

  // labels
  ctx.fillStyle = 'rgba(255,255,255,0.25)';
  ctx.font = `${10}px Inter, sans-serif`;
  ctx.fillText('60k — excluded', M + 4, yExcl - 4);
  ctx.fillText('25k — frontier', M + 4, yFlag - 4);
}

window.addEventListener('resize', resize);
resize();
