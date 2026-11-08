import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const source = readFileSync(new URL('./coi-serviceworker.js', import.meta.url), 'utf8');

function browser(isolated, stored = null) {
	const scripts = [];
	const errors = [];
	const storage = new Map(stored ? [['rclient-isolation-reload', stored]] : []);
	const context = {
		URL, setTimeout, clearTimeout,
		console: {error() {}},
		window: {crossOriginIsolated: isolated, isSecureContext: true},
		document: {
			currentScript: {src: 'https://example.com/project/coi-serviceworker.js'},
			createElement: () => ({}),
			body: {appendChild: script => scripts.push(script)}
		},
		navigator: {serviceWorker: {
			controller: {},
			ready: Promise.resolve(),
			async register(url) { context.registered = url; }
		}},
		sessionStorage: {
			getItem: key => storage.get(key) ?? null,
			setItem: (key, value) => storage.set(key, value),
			removeItem: key => storage.delete(key)
		},
		location: {reload() { context.reloaded = true; }},
		launchState: 'idle',
		launchStatus: {textContent: ''},
		launchButton: {click() { context.clicked = true; }},
		showLaunchError: message => errors.push(message)
	};
	return {context, scripts, errors, storage};
}

test('isolated server loads the client directly from its project directory', async () => {
	const {context, scripts} = browser(true, 'start');
	await vm.runInNewContext(source, context);
	assert.equal(context.registered, undefined);
	assert.equal(context.clicked, true);
	assert.equal(scripts[0].src, 'https://example.com/project/DDNet.js');
});

test('static host registers the worker and preserves an early launch request across one reload', async () => {
	const {context, scripts, storage} = browser(false);
	context.launchState = 'waiting';
	await vm.runInNewContext(source, context);
	assert.equal(context.registered, 'https://example.com/project/coi-serviceworker.js');
	assert.equal(context.reloaded, true);
	assert.equal(storage.get('rclient-isolation-reload'), 'start');
	assert.equal(scripts.length, 0);
});

test('failed isolation cannot cause an automatic reload loop', async () => {
	const {context, errors, scripts} = browser(false, 'reload');
	await vm.runInNewContext(source, context);
	assert.equal(context.reloaded, undefined);
	assert.equal(scripts.length, 0);
	assert.match(errors[0], /Could not enable/);
});

test('active worker reloads even when the first page remains uncontrolled', async () => {
	const {context, scripts, errors} = browser(false);
	context.navigator.serviceWorker.controller = null;
	await vm.runInNewContext(source, context);
	assert.equal(context.reloaded, true);
	assert.equal(scripts.length, 0);
	assert.deepEqual(errors, []);
});

test('browser preparation times out after 20 seconds without loading WASM', async () => {
	const {context, scripts, errors} = browser(false);
	context.navigator.serviceWorker.ready = new Promise(() => {});
	context.setTimeout = (callback, delay) => {
		assert.equal(delay, 20000);
		return setTimeout(callback, 1);
	};
	await vm.runInNewContext(source, context);
	assert.match(errors[0], /Browser setup timed out/);
	assert.equal(scripts.length, 0);
	assert.equal(context.reloaded, undefined);
});

test('unsupported browsers and failed worker registration show recovery', async () => {
	for (const unsupported of [true, false]) {
		const {context, errors, scripts} = browser(false);
		if (unsupported) {
			context.window.isSecureContext = false;
		} else {
			context.navigator.serviceWorker.register = async () => { throw new Error('Registration failed'); };
		}
		await vm.runInNewContext(source, context);
		assert.equal(errors.length, 1);
		assert.equal(scripts.length, 0);
		assert.equal(context.reloaded, undefined);
	}
});

test('worker isolates same-origin resources and leaves external CORS requests alone', async () => {
	const handlers = new Map();
	const context = {
		URL, Headers, Response,
		self: {
			location: {origin: 'https://example.com'},
			addEventListener: (name, callback) => handlers.set(name, callback)
		},
		fetch: async () => new Response('resource', {headers: {'Content-Type': 'application/wasm'}})
	};
	vm.runInNewContext(source, context);
	let response;
	handlers.get('fetch')({
		request: {url: 'https://example.com/project/DDNet.wasm'},
		respondWith: value => { response = value; }
	});
	response = await response;
	assert.equal(response.headers.get('Cross-Origin-Opener-Policy'), 'same-origin');
	assert.equal(response.headers.get('Cross-Origin-Embedder-Policy'), 'require-corp');
	assert.equal(response.headers.get('Content-Type'), 'application/wasm');
	assert.equal(await response.text(), 'resource');
	handlers.get('fetch')({
		request: {url: 'https://api.github.com/repos/RushieClient/RushieClient-ddnet/releases'},
		respondWith() { assert.fail('External requests must not be rewritten'); }
	});
});
