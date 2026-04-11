/* ═══════════════════════════════════════
   APP.JS — XPILE Transpiler Interface
═══════════════════════════════════════ */

/* ── STATE ── */
let currentLang = 'python';
let lastData    = null;
const TAB_IDS   = ['python', 'c', 'cpp', 'java', 'tokens'];

/* ── LOADING SCREEN ── */
const loadMessages = [
  'INITIALIZING ENGINE…',
  'LOADING LEXER…',
  'READY.',
];
let msgIdx = 0;
const loadStatus = document.getElementById('load-status');
const msgTimer = setInterval(() => {
  msgIdx++;
  if (msgIdx < loadMessages.length) {
    loadStatus.textContent = loadMessages[msgIdx];
  } else {
    clearInterval(msgTimer);
  }
}, 420);

window.addEventListener('load', () => {
  setTimeout(() => {
    document.getElementById('loader').classList.add('gone');
    init();
  }, 1200);
});

/* ── INIT ── */
function init() {
  buildExamples();
  loadExample(EXAMPLES.python[0]);
  updateLineNums();
}

/* ════════════════════════════════════════
   EXAMPLE PROGRAMS
════════════════════════════════════════ */
const EXAMPLES = {
  python: [
    { label: 'Arithmetic', code: `x = 10\ny = 3\nz = x + y\nresult = z * y\nprint z\nprint result` },
    { label: 'If / Else',  code: `a = 15\nb = 10\nif a > b :\n    big = a\nelse :\n    big = b\nprint big` },
    { label: 'While loop', code: `x = 1\nsum = 0\nwhile x < 6 :\n    sum = sum + x\n    x = x + 1\nprint sum` },
  ],
  c: [
    { label: 'Arithmetic', code: `#include <stdio.h>\nint main() {\n    int x = 10;\n    int y = 3;\n    int z = x + y;\n    printf("%d", z);\n    return 0;\n}` },
    { label: 'If / Else',  code: `#include <stdio.h>\nint main() {\n    int a = 15;\n    int b = 10;\n    if (a > b) {\n        int big = a;\n        printf("%d", big);\n    } else {\n        printf("%d", b);\n    }\n    return 0;\n}` },
    { label: 'For loop',   code: `#include <stdio.h>\nint main() {\n    int sum = 0;\n    for (int i = 1; i < 6; i++) {\n        sum = sum + i;\n    }\n    printf("%d", sum);\n    return 0;\n}` },
  ],
  cpp: [
    { label: 'Arithmetic', code: `#include <iostream>\nusing namespace std;\nint main() {\n    auto x = 10;\n    auto y = 3;\n    auto z = x + y;\n    cout << z << endl;\n    return 0;\n}` },
    { label: 'If / While', code: `#include <iostream>\nusing namespace std;\nint main() {\n    int x = 1;\n    int s = 0;\n    while (x < 6) {\n        s = s + x;\n        x = x + 1;\n    }\n    cout << s << endl;\n    return 0;\n}` },
    { label: 'For loop',   code: `#include <iostream>\nusing namespace std;\nint main() {\n    for (auto i = 0; i < 5; i++) {\n        auto sq = i * i;\n        cout << sq << endl;\n    }\n    return 0;\n}` },
  ],
  java: [
    { label: 'Arithmetic', code: `public class Test {\n    public static void main(String[] args) {\n        int x = 10;\n        int y = 3;\n        int z = x + y;\n        System.out.println(z);\n    }\n}` },
    { label: 'If / Else',  code: `public class Test {\n    public static void main(String[] args) {\n        int a = 20;\n        int b = 15;\n        if (a > b) {\n            int diff = a - b;\n            System.out.println(diff);\n        } else {\n            System.out.println(b);\n        }\n    }\n}` },
    { label: 'For loop',   code: `public class Test {\n    public static void main(String[] args) {\n        int sum = 0;\n        for (int i = 1; i < 6; i++) {\n            sum = sum + i;\n        }\n        System.out.println(sum);\n    }\n}` },
  ],
};

function buildExamples() {
  const grid = document.getElementById('ex-grid');
  grid.innerHTML = '';
  (EXAMPLES[currentLang] || []).forEach(ex => {
    const b = document.createElement('button');
    b.className = 'ex-btn';
    b.innerHTML = `<span class="ex-lang">${currentLang.toUpperCase()}</span>${ex.label}`;
    b.onclick = () => { loadExample(ex); toggleExamples(false); };
    grid.appendChild(b);
  });
}

function loadExample(ex) {
  document.getElementById('src').value = ex.code;
  updateLineNums();
}

/* ════════════════════════════════════════
   LANGUAGE SELECTION
════════════════════════════════════════ */
const LANG_CLASS = { python: 'nb-py', c: 'nb-c', cpp: 'nb-cpp', java: 'nb-java' };

function setLang(btn) {
  document.querySelectorAll('.lang-btn').forEach(b => b.classList.remove('active'));
  btn.classList.add('active');
  currentLang = btn.dataset.lang;

  const pill = document.getElementById('lang-pill');
  pill.innerHTML = `<span class="ndot"></span> ${currentLang}`;
  pill.className = 'nbadge ' + (LANG_CLASS[currentLang] || 'nb-py');
  pill.style.fontSize = '.58rem';

  buildExamples();
  const exs = EXAMPLES[currentLang];
  if (exs && exs.length) loadExample(exs[0]);
}

/* ════════════════════════════════════════
   PIPELINE ANIMATION
════════════════════════════════════════ */
function setPipeline(n) {
  for (let i = 0; i < 5; i++) {
    document.getElementById('ph' + i).classList.toggle('on', i <= n);
  }
  for (let i = 0; i < 4; i++) {
    const arr = document.getElementById('ar' + i);
    arr.classList.toggle('on', i < n);
    if (i === n - 1) {
      arr.classList.add('active');
      setTimeout(() => arr.classList.remove('active'), 700);
    }
  }
}

function resetPipeline() {
  for (let i = 0; i < 5; i++) document.getElementById('ph' + i).classList.remove('on');
  for (let i = 0; i < 4; i++) {
    const a = document.getElementById('ar' + i);
    a.classList.remove('on', 'active');
  }
}

function delay(ms) {
  return new Promise(r => setTimeout(r, ms));
}

/* ════════════════════════════════════════
   PARTICLE BURST
════════════════════════════════════════ */
function burstParticles(x, y) {
  const colors = ['var(--acc)', 'var(--amber)', 'var(--ice)', 'var(--violet)', 'var(--coral)'];
  for (let i = 0; i < 14; i++) {
    const p = document.createElement('div');
    p.className = 'particle';
    const angle = (i / 14) * Math.PI * 2;
    const dist  = 40 + Math.random() * 70;
    p.style.cssText = `
      left:${x}px; top:${y}px;
      --tx:${Math.cos(angle) * dist}px;
      --ty:${Math.sin(angle) * dist}px;
      --dur:${0.5 + Math.random() * 0.5}s;
      background:${colors[i % colors.length]}`;
    document.body.appendChild(p);
    setTimeout(() => p.remove(), 1100);
  }
}

/* ════════════════════════════════════════
   MAIN TRANSPILE FUNCTION
════════════════════════════════════════ */
async function transpile() {
  const src    = document.getElementById('src').value.trim();
  if (!src) return;

  const btn     = document.getElementById('btn-run');
  const status  = document.getElementById('status');
  const errWrap = document.getElementById('err-wrap');

  btn.disabled = true;
  btn.innerHTML = '<span class="spin"></span>&nbsp;Running…';
  status.className = 'status run';
  status.textContent = 'LEXING…';
  errWrap.className = 'err-wrap';
  resetPipeline();

  const steps = ['LEXING…', 'PARSING…', 'BUILDING IR…', 'GENERATING…', 'COMPLETE'];
  for (let i = 0; i < 4; i++) {
    await delay(110);
    setPipeline(i);
    status.textContent = steps[i];
  }

  let data;
  try {
    const res = await fetch('/transpile', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: currentLang + '\n' + src,
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    data = await res.json();
  } catch (e) {
    status.textContent = 'ERROR';
    status.className   = 'status err';
    document.getElementById('err-box').textContent =
      '⚠ Server unreachable.\nMake sure server.py is running:\n\n  python3 server.py\n\nor run:  bash run.sh';
    errWrap.className = 'err-wrap show';
    btn.disabled = false;
    btn.innerHTML = '<svg viewBox="0 0 12 12" fill="currentColor"><path d="M2 1l8 5-8 5V1z"/></svg> Transpile';
    return;
  }

  lastData = data;

  if (data.errors && data.errors.length) {
    document.getElementById('err-box').textContent = data.errors.join('\n');
    errWrap.className = 'err-wrap show';
  }

  await delay(100);
  setPipeline(4);
  status.className  = 'status ok';
  status.textContent = 'COMPLETE';

  // Particle burst
  const r = btn.getBoundingClientRect();
  burstParticles(r.left + r.width / 2, r.top + r.height / 2);

  /* ── IR TABLE ── */
  const irClassMap = {
    IR_ASSIGN:      'ir-assign',
    IR_BINOP:       'ir-binop',
    IR_PRINT:       'ir-print',
    IR_IF:          'ir-if',
    IR_ELSE:        'ir-if',
    IR_WHILE:       'ir-while',
    IR_FOR:         'ir-for',
    IR_RETURN:      'ir-other',
    IR_BLOCK_START: 'ir-other',
    IR_BLOCK_END:   'ir-other',
  };
  const tbody = document.getElementById('ir-body');
  tbody.innerHTML = '';
  if (!data.ir || !data.ir.length) {
    tbody.innerHTML = '<tr><td colspan="7" class="ph-txt" style="padding:20px 10px">No IR nodes generated.</td></tr>';
  } else {
    data.ir.forEach((n, i) => {
      const tr = document.createElement('tr');
      tr.className = 'fu';
      tr.style.animationDelay = (i * 20) + 'ms';
      tr.innerHTML = `
        <td style="color:var(--t3)">${i}</td>
        <td><span class="ir-tag ${irClassMap[n.type] || 'ir-other'}">${esc(n.type)}</span></td>
        <td style="color:var(--acc)">${esc(n.dest) || '—'}</td>
        <td style="color:var(--tx)">${esc(n.src1) || '—'}</td>
        <td style="color:var(--amber);font-weight:700">${esc(n.op) || '—'}</td>
        <td style="color:var(--tx)">${esc(n.src2) || '—'}</td>
        <td style="color:var(--t3)">${esc(n.dtype) || '—'}</td>`;
      tbody.appendChild(tr);
    });
  }

  /* ── TOKEN LIST ── */
  const tokColors = {
    IDENT:    'var(--tx)',
    NUMBER:   'var(--amber)',
    PRINT:    'var(--violet)',
    IF:       'var(--violet)',
    ELSE:     'var(--violet)',
    WHILE:    'var(--violet)',
    FOR:      'var(--violet)',
    RETURN:   'var(--violet)',
    INT:      'var(--ice)',
    FLOAT_KW: 'var(--ice)',
    CHAR_KW:  'var(--ice)',
    AUTO:     'var(--ice)',
    VOID:     'var(--ice)',
    BOOL:     'var(--ice)',
    ASSIGN:   'var(--acc)',
    PLUS:     'var(--acc)',
    MINUS:    'var(--acc)',
    STAR:     'var(--acc)',
    SLASH:    'var(--acc)',
    GT:       'var(--acc)',
    LT:       'var(--acc)',
    EQ:       'var(--acc)',
    NEQ:      'var(--acc)',
    STRING:   'var(--ember, #ff6b35)',
  };
  const tokLines = (data.tokens || []).map((t, i) => {
    const c = tokColors[t.type] || 'var(--t2)';
    return `<span style="color:var(--t3)">${String(i).padStart(3, ' ')}  </span>`
         + `<span style="color:${c};font-weight:700">${t.type.padEnd(14, ' ')}</span>`
         + `  <span style="color:var(--t2)">[${esc(t.lexeme)}]</span>`;
  });
  document.getElementById('out-tokens').innerHTML =
    `<span style="color:var(--t3);font-size:.63rem">No.  Type              Lexeme\n${'─'.repeat(44)}\n</span>`
    + tokLines.join('\n');

  /* ── CODE OUTPUTS ── */
  document.getElementById('out-python').innerHTML = hlPy(data.python   || '');
  document.getElementById('out-c').innerHTML      = hlC(data.c         || '');
  document.getElementById('out-cpp').innerHTML    = hlCpp(data.cpp     || '');
  document.getElementById('out-java').innerHTML   = hlJava(data.java   || '');

  /* ── STATS ── */
  const lines = ['python', 'c', 'cpp', 'java']
    .reduce((s, k) => s + (data[k] || '').split('\n').length, 0);
  ['st0', 'st1', 'st2'].forEach(id => {
    const el = document.getElementById(id);
    el.classList.add('lit');
    setTimeout(() => el.classList.remove('lit'), 900);
  });
  countUp('sv0', (data.tokens || []).length);
  countUp('sv1', (data.ir     || []).length);
  countUp('sv2', lines);

  document.getElementById('diff-btn').style.display = 'inline-block';

  btn.disabled  = false;
  btn.innerHTML = '<svg viewBox="0 0 12 12" fill="currentColor"><path d="M2 1l8 5-8 5V1z"/></svg> Transpile';
}

/* ════════════════════════════════════════
   SYNTAX HIGHLIGHTERS
════════════════════════════════════════ */
const esc = s => String(s)
  .replace(/&/g, '&amp;')
  .replace(/</g, '&lt;')
  .replace(/>/g, '&gt;');

/* ── SYNTAX HIGHLIGHTERS ─────────────────────────────────────────────────────
   Root cause of the "kw">, "cm"> artefacts:
   The old code called esc() first (turning < > into &lt; &gt;), then applied
   keyword regexes which inserted raw <span> tags into the string, and THEN
   ran comment/string regexes whose patterns like /(#[^\n]*)/ matched inside
   the already-inserted span tags (e.g. class="kw">) — corrupting the HTML.

   Fix: use a protect-then-restore strategy.
     1. HTML-escape the raw source.
     2. Pull out comments/strings and replace with opaque placeholders
        so NO subsequent regex can touch them.
     3. Apply keyword + number spans (now safe — no < > in the text).
     4. Restore placeholders wrapped in the correct span class.
   ─────────────────────────────────────────────────────────────────────────── */
function _hl(code, kwPattern) {
  let s = esc(code);                // step 1: HTML-escape

  const saved = [];
  function protect(re, cls) {
    s = s.replace(re, (m) => {
      saved.push({ text: m, cls });
      return '\x00' + (saved.length - 1) + '\x00';
    });
  }

  // step 2: protect in priority order (most specific first)
  protect(/(\/\*[\s\S]*?\*\/)/g,          'cm');   // block comments
  protect(/(\/\/[^\n]*)/g,                'cm');   // line comments
  protect(/(#[^\n]*)/g,                   'cm');   // Python / preprocessor lines
  protect(/(&quot;(?:[^&]|&(?!quot;))*&quot;)/g, 'str'); // "…" (already escaped)
  // Also catch the escaped form of double-quoted strings
  protect(/(\"(?:[^\"\\]|\\.)*\")/g,      'str');  // "…" (raw, if any slipped through)

  // step 3: keywords and numbers (no span tags present yet)
  s = s.replace(kwPattern, '<span class="kw">$1</span>');
  s = s.replace(/\b(\d+\.?\d*)\b/g, '<span class="num">$1</span>');

  // step 4: restore protected segments
  s = s.replace(/\x00(\d+)\x00/g, (_, i) => {
    const { text, cls } = saved[+i];
    return `<span class="${cls}">${text}</span>`;
  });

  return s;
}

function hlPy(code) {
  return _hl(code,
    /\b(print|if|else|while|for|in|range|return|pass|def|and|or|not|True|False|None)\b/g);
}
function hlC(code) {
  return _hl(code,
    /\b(int|float|char|void|double|auto|return|if|else|while|for|printf|scanf|include|define|main)\b/g);
}
function hlCpp(code) {
  return _hl(code,
    /\b(auto|int|float|char|void|double|return|if|else|while|for|cout|cin|endl|using|namespace|std|class|include|main)\b/g);
}
function hlJava(code) {
  return _hl(code,
    /\b(public|class|static|void|int|float|double|if|else|while|for|return|new|String|System|out|println|false|true|main)\b/g);
}
function hlForLang(code, lang) {
  if (lang === 'python' || lang === 'custom') return hlPy(code);
  if (lang === 'c')    return hlC(code);
  if (lang === 'cpp')  return hlCpp(code);
  if (lang === 'java') return hlJava(code);
  return esc(code);
}

/* ════════════════════════════════════════
   DIFF VIEW
════════════════════════════════════════ */
function showDiff() {
  if (!lastData) return;
  updateDiff();
  document.getElementById('diff-wrap').classList.add('show');
}
function updateDiff() {
  if (!lastData) return;
  const lang = document.getElementById('diff-lang').value;
  const src  = document.getElementById('src').value;
  const dst  = lastData[lang] || '';
  document.getElementById('diff-src').innerHTML = hlForLang(src, currentLang);
  document.getElementById('diff-dst').innerHTML = hlForLang(dst, lang);
  document.getElementById('diff-src-lbl').textContent = 'Source  (' + currentLang + ')';
  document.getElementById('diff-dst-lbl').textContent = 'Output  (' + lang + ')';
}
document.addEventListener('change', e => {
  if (e.target.id === 'diff-lang') updateDiff();
});

/* ════════════════════════════════════════
   HELPERS
════════════════════════════════════════ */
function switchTab(t) {
  TAB_IDS.forEach(id => {
    document.getElementById('out-' + id).classList.toggle('on', id === t);
    document.getElementById('tab-' + id).classList.toggle('on', id === t);
  });
}

function copyActive() {
  const el   = document.querySelector('.code-out.on');
  const text = el ? el.innerText : '';
  navigator.clipboard.writeText(text).then(() => {
    const b = document.getElementById('cpbtn');
    b.textContent = 'Copied!';
    b.classList.add('ok');
    setTimeout(() => { b.textContent = 'Copy'; b.classList.remove('ok'); }, 1500);
  });
}

function downloadActive() {
  if (!lastData) return;
  const active = document.querySelector('.code-out.on');
  if (!active) return;
  const id    = active.id.replace('out-', '');
  const exts  = { python: 'py', c: 'c', cpp: 'cpp', java: 'java', tokens: 'txt' };
  const names = { python: 'output', c: 'output', cpp: 'output', java: 'Output', tokens: 'tokens' };
  const blob  = new Blob([active.innerText], { type: 'text/plain' });
  const a     = document.createElement('a');
  a.href     = URL.createObjectURL(blob);
  a.download = names[id] + '.' + (exts[id] || 'txt');
  a.click();
}

function clearAll() {
  document.getElementById('src').value = '';
  document.getElementById('ir-body').innerHTML =
    '<tr><td colspan="7" class="ph-txt" style="padding:28px 10px">IR nodes will appear here after transpiling…</td></tr>';
  TAB_IDS.forEach(id =>
    document.getElementById('out-' + id).innerHTML =
      '<span class="ph-txt">// Output appears after transpiling…</span>'
  );
  ['sv0', 'sv1', 'sv2'].forEach(id => document.getElementById(id).textContent = '—');
  document.getElementById('status').textContent = 'READY';
  document.getElementById('status').className   = 'status';
  document.getElementById('err-wrap').className = 'err-wrap';
  document.getElementById('diff-btn').style.display = 'none';
  document.getElementById('diff-wrap').classList.remove('show');
  lastData = null;
  resetPipeline();
  updateLineNums();
}

function countUp(id, target) {
  const el   = document.getElementById(id);
  let v      = 0;
  const step = Math.max(1, Math.ceil(target / 18));
  const t    = setInterval(() => {
    v = Math.min(v + step, target);
    el.textContent = v;
    if (v >= target) clearInterval(t);
  }, 22);
}

function updateLineNums() {
  const ta = document.getElementById('src');
  const n  = ta.value.split('\n').length;
  document.getElementById('lnums').innerHTML =
    Array.from({ length: Math.max(n, 1) }, (_, i) =>
      `<span>${i + 1}</span>`
    ).join('');
}

function toggleGuide() {
  const b = document.getElementById('guide-body');
  const a = document.getElementById('guide-arrow');
  b.classList.toggle('open');
  a.classList.toggle('open');
}
function showGuide(lang, btn) {
  document.querySelectorAll('.gtab').forEach(b => b.classList.remove('on'));
  btn.classList.add('on');
  document.querySelectorAll('.syntax-block').forEach(b => b.classList.remove('on'));
  document.getElementById('guide-' + lang).classList.add('on');
}

function toggleExamples(forceClose) {
  const d = document.getElementById('ex-drawer');
  if (forceClose === false) { d.classList.remove('open'); return; }
  d.classList.toggle('open');
  buildExamples();
}

/* ── Keyboard shortcut ── */
document.getElementById('src').addEventListener('keydown', e => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
    e.preventDefault();
    transpile();
  }
});
document.getElementById('src').addEventListener('input', updateLineNums);
