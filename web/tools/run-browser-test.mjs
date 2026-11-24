// Runs one of the browser test pages in headless Chrome and prints its results; exits 1 if any check failed.
// Talks to Chrome over its DevTools protocol (Node's built-in WebSocket), so it waits for the test however long
// it takes, unlike --dump-dom.
//
//   node web/tools/run-browser-test.mjs http://127.0.0.1:8765/tests/browser.html [chrome executable]
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const url = process.argv[2];
const candidates = [process.argv[3], process.env.CHROME, 'google-chrome', 'C:/Program Files/Google/Chrome/Application/chrome.exe',
                    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].filter(Boolean);
const chrome = candidates.find(c => c.includes('/') && existsSync(c)) || candidates.find(c => !c.includes('/'));   // an installed path, else a name on the PATH
const port = 9300 + Math.floor(Math.random() * 500);
const profile = mkdtempSync(join(tmpdir(), 'mh-chrome-'));
const proc = spawn(chrome, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-sandbox', '--no-first-run',
                            '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--window-size=1600,1000', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
let exit = 1;
try {
  let target;
  for (let i = 0; i < 100 && !target; i++) {
    await sleep(200);
    try { target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find(t => t.type === 'page'); } catch { /* not up yet */ }
  }
  if (!target) throw new Error('Chrome did not start');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((ok, no) => { ws.onopen = ok; ws.onerror = no; });
  let id = 0;
  const pending = new Map();
  ws.onmessage = m => { const d = JSON.parse(m.data); if (pending.has(d.id)) { pending.get(d.id)(d.result); pending.delete(d.id); } };
  const send = (method, params = {}) => new Promise(r => { pending.set(++id, r); ws.send(JSON.stringify({ id, method, params })); });
  const evaluate = async expr => (await send('Runtime.evaluate', { expression: expr, returnByValue: true }))?.result?.value;
  await send('Page.enable');
  await send('Page.navigate', { url });
  let title = '';
  for (let i = 0; i < 600 && !/^(PASSED|FAILED)/.test(title); i++) { await sleep(200); title = (await evaluate('document.title')) || ''; }
  console.log(await evaluate("document.getElementById('out')?.textContent || '(no results)'"));
  if (!/^PASSED/.test(title)) console.log(`result: ${title || 'timed out'}`);
  exit = /^PASSED/.test(title) ? 0 : 1;
  ws.close();
} catch (e) { console.error(String(e.stack || e)); }
proc.kill();
await sleep(300);
try { rmSync(profile, { recursive: true, force: true }); } catch { /* Chrome may still hold it */ }
process.exit(exit);
