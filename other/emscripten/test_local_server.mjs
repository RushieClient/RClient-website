import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import vm from 'node:vm';

const clientSource = readFileSync(new URL('local_server.js', import.meta.url), 'utf8');
const workerSource = readFileSync(new URL('local_server_worker.js', import.meta.url), 'utf8');

function createClient() {
	const workers = [];
	const calls = [];
	const timers = new Map();
	let nextTimer = 0;
	class Worker {
		listeners = {};
		messages = [];
		constructor() { workers.push(this); }
		addEventListener(type, callback) { (this.listeners[type] ??= []).push(callback); }
		postMessage(message) { this.messages.push(message); }
		terminate() { this.terminated = true; }
		emit(type, data) { for (const callback of this.listeners[type] ?? []) callback({data, preventDefault() {}}); }
	}
	const Module = {calledRun: true, ccall: (...args) => calls.push(args)};
	vm.runInNewContext(clientSource, {
		Module, Worker, Uint8Array,
		window: {addEventListener() {}},
		setTimeout: callback => { timers.set(++nextTimer, callback); return nextTimer; },
		clearTimeout: id => timers.delete(id)
	});
	return {Module, server: Module.localServer, workers, calls, timers};
}

test('ready server routes packets for both player and dummy', async () => {
	const {Module, server, workers, calls} = createClient();
	server.start(['sv_map Tutorial']);
	await Promise.resolve();
	const worker = workers[0];
	assert.equal(server.state, 1);
	worker.emit('message', {type: 'ready'});
	assert.equal(server.state, 2);
	for (const port of [49152, 49153]) {
		Module.localNetworkSend(port, 8303, new Uint8Array([1, 2]));
		assert.equal(worker.messages.at(-1).source, port);
		worker.emit('message', {type: 'packet', source: 8303, destination: port, data: new Uint8Array([3])});
		assert.equal(calls.at(-1)[3][1], port);
	}
	const count = worker.messages.length;
	Module.localNetworkSend(49152, 9999, new Uint8Array([1]));
	assert.equal(worker.messages.length, count);
});

test('stop acknowledges cleanup and restart ignores old server messages', async () => {
	const {server, workers} = createClient();
	server.start([]);
	await Promise.resolve();
	const old = workers[0];
	server.start([]);
	assert.equal(old.messages.at(-1).type, 'stop');
	old.emit('message', {type: 'stopped'});
	await Promise.resolve();
	assert.equal(old.terminated, true);
	assert.equal(workers.length, 2);
	old.emit('message', {type: 'error', message: 'stale'});
	assert.equal(server.error, '');
	workers[1].emit('message', {type: 'ready'});
	assert.equal(server.state, 2);
});

test('cancel before Worker creation and rapid starts do not create duplicate servers', async () => {
	const {server, workers} = createClient();
	server.start([]);
	server.stop();
	await Promise.resolve();
	assert.equal(workers.length, 0);
	server.start([]);
	server.start([]);
	await Promise.resolve();
	assert.equal(workers.length, 1);
});

test('load error and startup timeout stop the server and allow retry', async () => {
	const {server, workers, timers} = createClient();
	server.start([]);
	await Promise.resolve();
	workers[0].emit('message', {type: 'error', message: 'missing wasm'});
	assert.equal(server.state, -1);
	assert.equal(server.error, 'missing wasm');
	workers[0].emit('message', {type: 'stopped'});
	server.start([]);
	await Promise.resolve();
	assert.equal(server.error, '');
	const timeout = [...timers.values()][0];
	timeout();
	assert.equal(server.state, -1);
	assert.match(server.error, /60 seconds/);
});

test('server host starts Tutorial and terminates every pthread Worker', () => {
	let receive;
	const children = [];
	const messages = [];
	const context = {
		console, URL, Uint8Array,
		FS: {mkdirTree: path => assert.equal(path, '/data/mapres')},
		location: {href: 'http://localhost/local_server_worker.js'},
		Worker: class {constructor() { children.push(this); } terminate() { this.terminated = true; }},
		addEventListener: (_, callback) => { receive = callback; },
		postMessage: message => messages.push(message),
		close() { this.closed = true; },
		importScripts: filename => assert.equal(filename, 'DDNet-Server.js')
	};
	context.self = context;
	vm.runInNewContext(workerSource, context);
	receive({data: {type: 'start', args: ['auth_add local admin test']}});
	assert.ok(context.Module.arguments.includes('sv_map Tutorial'));
	assert.ok(context.Module.arguments.includes('sv_max_clients_per_ip 2'));
	context.Module.preRun[0]();
	new context.Worker('pthread');
	new context.Worker('pthread');
	receive({data: {type: 'stop'}});
	assert.ok(children.every(child => child.terminated));
	assert.equal(messages.at(-1).type, 'stopped');
	assert.equal(context.closed, true);
});

test('page cleanup does not call WASM before initialization or after exit', () => {
	const {Module, server, calls} = createClient();
	Module.calledRun = false;
	server.stop();
	Module.calledRun = true;
	server.closed = true;
	server.stop();
	assert.equal(calls.length, 0);
});
