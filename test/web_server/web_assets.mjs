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
for (const fetch of [
  async () => new Response('{"error":"out of memory"}', {status: 503}),
  async () => new Response('internal error', {status: 500}),
  async () => { throw new Error('connection lost'); },
]) {
  const empty = {hidden: true}, message = {};
  const page = vm.createContext({window: {}, fetch, document: {
    getElementById: id => id === 'empty-state' ? empty : {children: []},
    querySelector: () => message,
  }});
  vm.runInContext(hostCode, page);
  await vm.runInContext(plugins, page);
  assert.equal(empty.hidden, false);
  assert.match(message.textContent, /could not load plugins/i);
}

function section(source, from, until) {
  const a = source.indexOf(from), b = source.indexOf(until, a + 1);
  assert.ok(a >= 0 && b > a);
  return source.slice(a, b);
}
for (const hostAvailable of [false, true]) {
  const elements = new Map(), loads = [];
  const element = () => ({appendChild() {}, replaceChildren() {}, remove() {}});
  const page = vm.createContext({
    currentPath: '/', setTimeout, clearTimeout, escapeHtml: s => s,
    document: {
      createElement: element, querySelectorAll: () => [],
      getElementById(id) {
        if (!elements.has(id)) elements.set(id, element());
        return elements.get(id);
      },
      head: {appendChild(script) { loads.push(script.src); page.JSZip = {}; script.onload(); }},
      body: {appendChild(script) { loads.push(script.src); script.onload(); }},
    },
    fetch: async url => {
      if (url === '/api/plugins') return Response.json([{mount: 'settings'}, {mount: 'files', name: 'one'},
                                                        {mount: 'files', name: 'two'}]);
      assert.match(url, /^\/api\/files\?path=%2F/);
      return Response.json([{name: 'Books', isDirectory: true, size: 0}]);
    },
  });
  page.window = page;
  for (const name of ['fetchVersion', 'closeUploadModal', 'closeFolderModal', 'closeDeleteModal',
                      'closeRenameModal', 'closeMoveModal', 'closeImagePreview']) page[name] = () => {};
  vm.runInContext(files.slice(start, end), page);
  if (hostAvailable) vm.runInContext(hostCode, page);
  vm.runInContext(section(files, 'async function hydrate()', '// Image preview'), page);
  await page.hydrate();
  await new Promise(resolve => setImmediate(resolve));
  assert.match(elements.get('file-table').innerHTML, /href="\/files\?path=%2FBooks"/);
  assert.deepEqual(loads, hostAvailable ? ['/js/jszip.min.js', '/plugin?name=one&file=plugin.js',
                                            '/plugin?name=two&file=plugin.js'] : []);
}

const epub = new File([readFileSync(new URL('../epubs/test_br_section_break.epub', import.meta.url))],
                      'book.epub', {type: 'application/epub+zip'});
const alerts = [];
let cleared = false;
const picker = vm.createContext({setTimeout, clearTimeout,
  document: {createElement: () => ({remove() {}}), head: {appendChild: s => s.onerror()}},
  showWebAlert: msg => alerts.push(msg), clearImagePicker: () => cleared = true,
});
vm.runInContext(files.slice(start, end) + section(files, 'async function showImagePicker', 'function renderImageGrid'), picker);
await picker.showImagePicker(epub);
assert.match(alerts[0], /upload button to continue/);
assert.ok(cleared);

const settings = read('SettingsPage.html');
const edit = vm.createContext({escapeHtml: s => s});
vm.runInContext(section(settings, 'function enableItemSave', 'function renderWifiSection') +
                section(settings, 'const OPDS_MAX_CUSTOM_HEADERS', 'function renderOpdsSection'), edit);
for (const row of [edit.renderWifiNetwork({ssid: 'saved'}, 1),
                   edit.renderOpdsServer({name: 'saved', url: 'https://example.test'}, 1)]) {
  assert.match(row, /value="saved"/);
  const save = {disabled: /<button [^>]*btn-save-server[^>]* disabled>/.test(row)};
  assert.ok(save.disabled);
  edit.box = {querySelector(selector) { assert.equal(selector, '.btn-save-server'); return save; }};
  vm.runInContext('(function(){' + row.match(/oninput="([^"]+)"/)[1] + '}).call(box)', edit);
  assert.equal(save.disabled, false);
}

const descriptors = readFileSync(new URL('../../src/SettingsList.cpp', import.meta.url), 'utf8').split('emit(');
for (const key of ['clockUtcOffsetQ', 'clockTimezone']) {
  const entry = descriptors.find(d => d.split(';')[0].includes('"' + key + '"'));
  assert.match(entry?.split(';')[0] ?? '', /\.withWebHidden\(\)/);
}

let uploaded;
Object.assign(picker, {
  currentIndex: 0, files: [epub], failedFiles: [], convertEnabled: true,
  useBatchLog: false, useWebSocket: false, operationCancelled: false, exportLogCheckbox: null,
  progressFill: {style: {}, classList: {add() {}, remove() {}}}, progressText: {style: {}},
  setTimeout() {}, clearLog() {}, showLog() {}, logError() {}, log() {},
  reserveAvailableUploadFilename: name => name, usedFileNames: new Set(),
  uploadFileHTTP: async file => { uploaded = file; },
});
picker.document.getElementById = () => ({checked: false});
vm.runInContext(section(files, 'async function uploadNextFile()', '\n  uploadNextFile();\n}'), picker);
await picker.uploadNextFile();
assert.equal(uploaded, epub);

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
