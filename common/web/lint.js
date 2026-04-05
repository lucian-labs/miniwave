#!/usr/bin/env node
// Syntax-check all inline <script> blocks and standalone .js files in web/
const fs = require('fs');
const path = require('path');

const dir = __dirname;
let errors = 0;
let checked = 0;

// Check inline scripts in HTML files
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.html'))) {
  const html = fs.readFileSync(path.join(dir, f), 'utf8');
  const scripts = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)];
  for (let i = 0; i < scripts.length; i++) {
    checked++;
    try { new Function(scripts[i][1]); }
    catch (e) {
      console.error(`${f} script[${i}]: ${e.message}`);
      errors++;
    }
  }
}

// Check standalone .js files
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.js') && f !== 'lint.js')) {
  checked++;
  const src = fs.readFileSync(path.join(dir, f), 'utf8');
  try { new Function(src); }
  catch (e) {
    console.error(`${f}: ${e.message}`);
    errors++;
  }
}

if (errors) {
  console.error(`FAIL: ${errors} syntax error(s) in ${checked} sources`);
  process.exit(1);
}
console.log(`OK: ${checked} sources checked, no syntax errors`);
