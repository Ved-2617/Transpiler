/* ═══════════════════════════════════════
   HOME.JS — XPILE Landing Page
═══════════════════════════════════════ */

/* ── NAV SCROLL BEHAVIOR ── */
const nav = document.getElementById('nav');
window.addEventListener('scroll', () => {
  nav.classList.toggle('scrolled', window.scrollY > 40);
}, { passive: true });

/* ── MOBILE MENU ── */
const burger = document.getElementById('burger');
const mobileMenu = document.getElementById('mobile-menu');
burger.addEventListener('click', () => {
  mobileMenu.classList.toggle('open');
});
function closeMobile() {
  mobileMenu.classList.remove('open');
}

/* ── SCROLL REVEAL ── */
const revealEls = document.querySelectorAll(
  '.reveal, .reveal-up, .reveal-left, .reveal-right'
);

const observer = new IntersectionObserver((entries) => {
  entries.forEach(e => {
    if (e.isIntersecting) {
      e.target.classList.add('visible');
    }
  });
}, { threshold: 0.12, rootMargin: '0px 0px -40px 0px' });

revealEls.forEach(el => observer.observe(el));

// Pipeline steps get 'visible' class too for their special styling
const pvSteps = document.querySelectorAll('.pv-step');
const pvObserver = new IntersectionObserver((entries) => {
  entries.forEach(e => {
    if (e.isIntersecting) e.target.classList.add('visible');
  });
}, { threshold: 0.2 });
pvSteps.forEach(el => pvObserver.observe(el));

/* ── HERO CODE CARD ── */
const CARD_SNIPPETS = {
  python: {
    label: 'Python',
    code: [
      { type: 'cm',  text: '# fibonacci sequence' },
      { type: 'vr',  text: 'a' },
      { type: 'tx',  text: ' = ' },
      { type: 'num', text: '0' },
      { type: 'tx',  text: '\n' },
      { type: 'vr',  text: 'b' },
      { type: 'tx',  text: ' = ' },
      { type: 'num', text: '1' },
      { type: 'tx',  text: '\n' },
      { type: 'kw',  text: 'while' },
      { type: 'tx',  text: ' a < ' },
      { type: 'num', text: '100' },
      { type: 'tx',  text: ' :\n    ' },
      { type: 'kw',  text: 'print' },
      { type: 'tx',  text: ' a\n    ' },
      { type: 'vr',  text: 'tmp' },
      { type: 'tx',  text: ' = a + b\n    ' },
      { type: 'vr',  text: 'a' },
      { type: 'tx',  text: ' = b\n    ' },
      { type: 'vr',  text: 'b' },
      { type: 'tx',  text: ' = tmp' },
    ]
  },
  c: {
    label: 'C',
    code: [
      { type: 'cm',  text: '// fibonacci sequence' },
      { type: 'tx',  text: '\n' },
      { type: 'tp',  text: 'int' },
      { type: 'tx',  text: ' a = ' },
      { type: 'num', text: '0' },
      { type: 'tx',  text: ';\n' },
      { type: 'tp',  text: 'int' },
      { type: 'tx',  text: ' b = ' },
      { type: 'num', text: '1' },
      { type: 'tx',  text: ';\n' },
      { type: 'kw',  text: 'while' },
      { type: 'tx',  text: ' (a < ' },
      { type: 'num', text: '100' },
      { type: 'tx',  text: ') {\n    ' },
      { type: 'fn',  text: 'printf' },
      { type: 'tx',  text: '(' },
      { type: 'str', text: '"%d"' },
      { type: 'tx',  text: ', a);\n    ' },
      { type: 'tp',  text: 'int' },
      { type: 'tx',  text: ' tmp = a + b;\n    a = b;\n    b = tmp;\n}' },
    ]
  },
  cpp: {
    label: 'C++',
    code: [
      { type: 'cm',  text: '// fibonacci sequence' },
      { type: 'tx',  text: '\n' },
      { type: 'kw',  text: 'auto' },
      { type: 'tx',  text: ' a = ' },
      { type: 'num', text: '0' },
      { type: 'tx',  text: ';\n' },
      { type: 'kw',  text: 'auto' },
      { type: 'tx',  text: ' b = ' },
      { type: 'num', text: '1' },
      { type: 'tx',  text: ';\n' },
      { type: 'kw',  text: 'while' },
      { type: 'tx',  text: ' (a < ' },
      { type: 'num', text: '100' },
      { type: 'tx',  text: ') {\n    ' },
      { type: 'fn',  text: 'cout' },
      { type: 'tx',  text: ' << a << ' },
      { type: 'fn',  text: 'endl' },
      { type: 'tx',  text: ';\n    ' },
      { type: 'kw',  text: 'auto' },
      { type: 'tx',  text: ' tmp = a + b;\n    a = b;\n    b = tmp;\n}' },
    ]
  },
  java: {
    label: 'Java',
    code: [
      { type: 'cm',  text: '// fibonacci sequence' },
      { type: 'tx',  text: '\n' },
      { type: 'tp',  text: 'int' },
      { type: 'tx',  text: ' a = ' },
      { type: 'num', text: '0' },
      { type: 'tx',  text: ';\n' },
      { type: 'tp',  text: 'int' },
      { type: 'tx',  text: ' b = ' },
      { type: 'num', text: '1' },
      { type: 'tx',  text: ';\n' },
      { type: 'kw',  text: 'while' },
      { type: 'tx',  text: ' (a < ' },
      { type: 'num', text: '100' },
      { type: 'tx',  text: ') {\n    System.out.' },
      { type: 'fn',  text: 'println' },
      { type: 'tx',  text: '(a);\n    ' },
      { type: 'tp',  text: 'int' },
      { type: 'tx',  text: ' tmp = a + b;\n    a = b;\n    b = tmp;\n}' },
    ]
  }
};

const typeColors = {
  kw:  'var(--kw, #9d4edd)',
  fn:  'var(--fn, #00d4ff)',
  str: 'var(--str,#00ffb3)',
  num: 'var(--num,#ffb300)',
  cm:  'var(--cm, #444455)',
  tp:  'var(--tp, #00d4ff)',
  vr:  'var(--acc,#00ffb3)',
  tx:  'inherit'
};

function renderCardCode(lang) {
  const snippet = CARD_SNIPPETS[lang];
  if (!snippet) return;

  document.getElementById('card-lang-label').textContent = snippet.label;

  const body = document.getElementById('code-card-body');
  body.innerHTML = '';

  // Animate typing character by character
  let fullHtml = '';
  snippet.code.forEach(tok => {
    const escaped = tok.text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
    if (tok.type === 'tx') {
      fullHtml += escaped;
    } else {
      const color = typeColors[tok.type] || 'inherit';
      fullHtml += `<span style="color:${color}">${escaped}</span>`;
    }
  });

  // Typewriter effect
  body.style.opacity = '0';
  body.style.transform = 'translateY(6px)';
  body.style.transition = 'opacity .25s ease, transform .25s ease';

  setTimeout(() => {
    body.innerHTML = fullHtml;
    body.style.opacity = '1';
    body.style.transform = 'translateY(0)';
  }, 150);
}

// Language tab switching
document.querySelectorAll('.ltab').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.ltab').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    renderCardCode(btn.dataset.lang);
  });
});

// Auto-cycle languages
let autoLangIdx = 0;
const langOrder = ['python', 'c', 'cpp', 'java'];

function autoAdvanceLang() {
  autoLangIdx = (autoLangIdx + 1) % langOrder.length;
  const lang = langOrder[autoLangIdx];
  document.querySelectorAll('.ltab').forEach(b => {
    b.classList.toggle('active', b.dataset.lang === lang);
  });
  renderCardCode(lang);
}

renderCardCode('python');
let autoInterval = setInterval(autoAdvanceLang, 3800);

// Pause auto-cycle on user interaction
document.querySelectorAll('.ltab').forEach(btn => {
  btn.addEventListener('click', () => {
    clearInterval(autoInterval);
    autoInterval = null;
  });
});

/* ── SMOOTH ANCHOR LINKS ── */
document.querySelectorAll('a[href^="#"]').forEach(a => {
  a.addEventListener('click', e => {
    const id = a.getAttribute('href').slice(1);
    const el = document.getElementById(id);
    if (el) {
      e.preventDefault();
      el.scrollIntoView({ behavior: 'smooth', block: 'start' });
    }
  });
});
