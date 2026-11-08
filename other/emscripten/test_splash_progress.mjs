import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const start = html.indexOf('totalDependencies: 0,');
const end = html.indexOf("canvas: document.getElementById('canvas')", start);

function progress() {
	const context = {
		runtimeReady: false,
		launchState: 'waiting',
		launchStatus: {textContent: ''},
		launchButtonProgress: {style: {}},
		loadingBar: {setAttribute(name, value) { this[name] = value; }}
	};
	const module = vm.runInNewContext(`({${html.slice(start, end)}})`, context);
	return {context, module};
}

test('data progress reports actual bytes and adds concurrent packages', () => {
	const {context, module} = progress();
	module.dataFileDownloads = {
		client: {loaded: 16 * 1048576, total: 80 * 1048576},
		assets: {loaded: 4 * 1048576, total: 20 * 1048576}
	};
	module.setStatus('Downloading data... (20971520/104857600)');
	assert.equal(context.launchStatus.textContent, 'Downloading client data: 20.0 / 100.0 MB (20%)');
	assert.equal(context.launchButtonProgress.style.width, '20%');
	assert.equal(context.loadingBar['aria-valuenow'], 20);
	module.monitorRunDependencies(0);
	assert.equal(context.launchButtonProgress.style.width, '20%');
});

test('download progress received before clicking remains available when waiting', () => {
	const {context, module} = progress();
	context.launchState = 'idle';
	module.dataFileDownloads = {client: {loaded: 5 * 1048576, total: 10 * 1048576}};
	module.setStatus();
	context.launchState = 'waiting';
	module.setStatus();
	assert.equal(context.launchStatus.textContent, 'Downloading client data: 5.0 / 10.0 MB (50%)');
});

test('runtime progress remains independent of byte progress', () => {
	const {context, module} = progress();
	module.monitorRunDependencies(2);
	module.monitorRunDependencies(1);
	assert.equal(context.launchButtonProgress.style.width, '50%');
	module.dataFileDownloads = {client: {loaded: 5, total: 10}};
	for (const state of ['starting', 'running', 'error', 'closed']) {
		context.launchState = state;
		context.launchStatus.textContent = state;
		module.setStatus();
		assert.equal(context.launchStatus.textContent, state);
	}
	context.launchState = 'waiting';
	context.runtimeReady = true;
	module.setStatus();
	assert.equal(context.launchStatus.textContent, 'closed');
});
