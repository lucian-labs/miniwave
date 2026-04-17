#!/usr/bin/env node
// Syntax-check + paranoid static analysis for miniwave web sources
const fs = require('fs');
const path = require('path');

const dir = __dirname;
let errors = 0;
let warnings = 0;
let checked = 0;

function warn(file, line, msg) {
  console.error(`  WARN ${file}:${line} — ${msg}`);
  warnings++;
}
function fail(file, msg) {
  console.error(`  FAIL ${file}: ${msg}`);
  errors++;
}

function analyzeSource(file, src) {
  const lines = src.split('\n');

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const ln = i + 1;

    // getElementById/querySelector result used immediately without null check
    // e.g. document.getElementById('foo').addEventListener — crash if null
    const directAccess = line.match(
      /document\.(getElementById|querySelector|querySelectorAll)\s*\([^)]+\)\s*\.\s*(addEventListener|classList|style|textContent|innerHTML|value|checked|onclick|className|appendChild|insertBefore|removeChild|setAttribute|getAttribute|focus|blur|click|remove|prepend|append)/
    );
    if (directAccess) {
      // Check if it's guarded: preceded by `if (`, `&&`, `?.`, or assigned to var first
      const trimmed = line.trim();
      if (!trimmed.startsWith('if') && !trimmed.startsWith('//') &&
          !line.includes('?.') && !line.includes('&&') &&
          !line.match(/(?:const|let|var)\s+\w+\s*=\s*document\./)) {
        warn(file, ln, `unguarded DOM access: ${directAccess[0].slice(0, 60)}`);
      }
    }

    // Duplicate top-level const/let (not inside { } blocks)
    // Only flags declarations at indent level 0 (no leading whitespace beyond the base)
    const declMatch = line.match(/^(?:const|let)\s+(\w+)\s*=/);
    if (declMatch) {
      const varName = declMatch[1];
      const re = new RegExp(`^(?:const|let)\\s+${varName}\\s*=`, 'gm');
      const matches = src.match(re);
      if (matches && matches.length > 1) {
        const firstIdx = src.indexOf(matches[0]);
        const currentPos = lines.slice(0, i).join('\n').length;
        if (currentPos <= firstIdx + 5) {
          warn(file, ln, `top-level "${varName}" declared ${matches.length} times`);
        }
      }
    }

    // Common typos: referencing 'es.' when EventSource is named 'evtSource'
    if (line.match(/^es\./) && src.includes('evtSource') && !src.match(/\blet\s+es\b|\bconst\s+es\b|\bvar\s+es\b/)) {
      warn(file, ln, `"es." used but EventSource appears to be named "evtSource"`);
    }
  }
}

// Check inline scripts in HTML files
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.html'))) {
  const html = fs.readFileSync(path.join(dir, f), 'utf8');
  const scripts = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)];
  for (let i = 0; i < scripts.length; i++) {
    checked++;
    try { new Function(scripts[i][1]); }
    catch (e) {
      fail(f + ` script[${i}]`, e.message);
    }
    analyzeSource(f, scripts[i][1]);
  }
}

// Check standalone .js files
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.js') && f !== 'lint.js')) {
  checked++;
  const src = fs.readFileSync(path.join(dir, f), 'utf8');
  try { new Function(src); }
  catch (e) {
    fail(f, e.message);
  }
  analyzeSource(f, src);
}

if (errors) {
  console.error(`FAIL: ${errors} error(s), ${warnings} warning(s) in ${checked} sources`);
  process.exit(1);
}
if (warnings) {
  console.log(`OK: ${checked} sources, ${warnings} warning(s) — review above`);
} else {
  console.log(`OK: ${checked} sources checked, no issues`);
}
