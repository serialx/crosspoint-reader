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
await vm.runInNewContext(code, {loadPlugins: mount => { mounted = mount; return true; }});
assert.equal(mounted, null);
await vm.runInNewContext(code, {
  document: {getElementById: () => status},
  setTimeout: (_, ms) => delay = ms,
});
assert.match(status.textContent, /failed to load/i);
assert.equal(delay, 10000);

const plugins = read('PluginsPage.html').match(/<script>([\s\S]*?)<\/script>/)[1];
for (const [success, count] of [[true, 1], [true, 0], [false, 0]]) {
  const empty = {hidden: true}, message = {textContent: 'empty'};
  await vm.runInNewContext(plugins, {
    loadPlugins: async mount => { assert.equal(mount, 'settings'); return success; },
    document: {
      getElementById: id => id === 'empty-state' ? empty : {children: Array(count)},
      querySelector: () => message,
    },
  });
  assert.equal(empty.hidden, count > 0);
  assert.equal(message.textContent === 'empty', success);
}
const header = read('shared/Header.html.inc').match(/<script>([\s\S]*?)<\/script>/)[1];
for (const enabled of [false, true]) {
  let removed = 0;
  vm.runInNewContext(header, {document: {
    cookie: 'other=1; cpPlugins=' + Number(enabled),
    querySelectorAll: () => Array.from({length: 2}, () => ({remove() {++removed;}})),
  }});
  assert.equal(removed, enabled ? 0 : 2);
}

const hostCode = read('js/plugin-host.js');

const sent = [];
const fetchPage = vm.createContext({window: {}, fetch: async (path, request) => {
  assert.equal(path, '/api/fetch');
  sent.push(JSON.parse(request.body));
  return Response.json({status: 200, complete: true});
}});
vm.runInContext(hostCode, fetchPage);
await fetchPage.window.CrossPoint.fetchToSd('test', 'https://example.test/catalog', '/books/new.epub',
                                          {'X-App': 'from-browser'}, {method: 'post', body: 'a\0b'});
assert.deepEqual(sent, [{plugin: 'test', url: 'https://example.test/catalog', dest: '/books/new.epub',
                         headers: {'X-App': 'from-browser'}, method: 'POST', body: 'a\0b'}]);
