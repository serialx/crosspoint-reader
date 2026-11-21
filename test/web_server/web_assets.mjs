import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const read = name => readFileSync(new URL('../../src/network/html/' + name, import.meta.url), 'utf8');
const files = read('FilesPage.html');
const scripts = [];
const context = vm.createContext({
  setTimeout, clearTimeout,
  document: {
    createElement: () => ({remove() {}}),
    head: {appendChild: el => scripts.push(el)},
  },
});
const start = files.indexOf('const SCRIPT_LOAD_TIMEOUT_MS');
const end = files.indexOf('async function maybeRenameEbookFile', start);
assert.ok(start >= 0 && end > start);
vm.runInContext(files.slice(start, end), context);
assert.equal(scripts.length, 0);
const pending = context.ensureEpubTools();
assert.equal(context.ensureEpubTools(), pending);
assert.equal(scripts[0].src, '/js/jszip.min.js');
context.JSZip = {};
scripts[0].onload();
await Promise.resolve();
assert.equal(scripts[1].src, '/js/epub-optimizer.js');
scripts[1].onerror();
await assert.rejects(pending);
const retry = context.ensureEpubTools();
await Promise.resolve();
assert.equal(scripts[2].src, scripts[1].src);
scripts[2].onload();
await retry;
assert.equal(scripts.length, 3);

const runner = read('RunnerPage.html');
assert.match(runner, /<script src="\/js\/plugin-host.js"><\/script>/);
const code = runner.match(/<script>([\s\S]*?)<\/script>/)[1];
let mounted, delay, status = {};
vm.runInNewContext(code, {loadPlugins: mount => mounted = mount});
assert.equal(mounted, null);
vm.runInNewContext(code, {
  document: {getElementById: () => status},
  setTimeout: (_, ms) => delay = ms,
});
assert.match(status.textContent, /failed to load/i);
assert.equal(delay, 10000);
