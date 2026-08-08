const asLayer = document.getElementById('layerAs');
const naLayer = document.getElementById('layerNa');
const asEls = [];
const naEls = [];

function rand(a, b) {
  return a + Math.random() * (b - a);
}

for (let i = 0; i < 50; i++) {
  const el = document.createElement('div');
  el.className = 'dot';
  const s = rand(2, 8);
  el.style.width = s + 'px';
  el.style.height = s + 'px';
  el.style.left = rand(0, 100) + '%';
  el.style.top = rand(0, 100) + '%';
  el.style.background = `hsla(${rand(225, 245)}, 45%, 60%, ${rand(0.12, 0.35)})`;
  asLayer.appendChild(el);
  asEls.push({ el, depth: rand(0.15, 0.45) });
}

for (let i = 0; i < 30; i++) {
  const el = document.createElement('div');
  el.className = 'bar';
  el.style.width = rand(1, 2.5) + 'px';
  el.style.height = rand(16, 70) + 'px';
  el.style.left = rand(0, 100) + '%';
  el.style.top = rand(0, 100) + '%';
  el.style.background = `hsla(${rand(14, 30)}, 65%, 50%, ${rand(0.06, 0.16)})`;
  naLayer.appendChild(el);
  naEls.push({ el, depth: rand(0.55, 0.95) });
}

for (let i = 0; i < 40; i++) {
  const el = document.createElement('div');
  el.className = 'dot';
  const s = rand(1, 4);
  el.style.width = s + 'px';
  el.style.height = s + 'px';
  el.style.left = rand(0, 100) + '%';
  el.style.top = rand(0, 100) + '%';
  el.style.background = `hsla(${rand(340, 360)}, 55%, 65%, ${rand(0.08, 0.2)})`;
  naLayer.appendChild(el);
  naEls.push({ el, depth: rand(0.65, 1.1) });
}

let ticking = false;

function update() {
  const scrollY = window.scrollY;
  const maxScroll = document.body.scrollHeight - window.innerHeight;
  const progress = maxScroll > 0 ? Math.min(scrollY / maxScroll, 1) : 0;

  for (const d of asEls) {
    d.el.style.transform = `translateY(${progress * 180 * d.depth}px)`;
  }
  for (const d of naEls) {
    d.el.style.transform = `translateY(${progress * 420 * d.depth}px)`;
  }

  ticking = false;
}

window.addEventListener('scroll', () => {
  if (!ticking) {
    requestAnimationFrame(update);
    ticking = true;
  }
}, { passive: true });

update();
