// Keep this host Worker's event loop free; the server main runs on a pthread.
const childWorkers = new Set();
const NativeWorker = self.Worker;
self.Worker = class extends NativeWorker {
	constructor(...args) {
		super(...args);
		childWorkers.add(this);
	}
};

self.addEventListener('message', event => {
	const message = event.data;
	if (message.type === 'stop') {
		for (const worker of childWorkers)
			worker.terminate();
		childWorkers.clear();
		self.postMessage({type: 'stopped'});
		self.close();
	} else if (message.type === 'start' && !self.Module) {
		self.Module = {
			noInitialRun: true,
			preRun: [() => FS.mkdirTree('/data/mapres')],
			mainScriptUrlOrBlob: new URL('DDNet-Server.js', self.location.href).href,
			arguments: message.args.concat([
				'sv_map Tutorial', 'sv_register 0', 'sv_port 8303', 'bindaddr 127.0.0.1',
				'sv_ipv4only 1', 'sv_sixup 0', 'ec_port 0', 'sv_use_sql 0',
				'sv_dnsbl 0', 'sv_max_clients 2', 'sv_max_clients_per_ip 2'
			]),
			localNetworkSend(source, destination, data) {
				self.postMessage({type: 'packet', source, destination, data}, [data.buffer]);
			},
			onRuntimeInitialized() {
				Module.callMain(Module.arguments);
			},
			onAbort(reason) {
				self.postMessage({type: 'error', message: `Local server aborted: ${reason}`});
			},
			onExit(code) {
				self.postMessage({type: 'error', message: `Local server exited (${code}).`});
			},
			print(text) { console.debug('[local server]', text); },
			printErr(text) { console.error('[local server]', text); }
		};
		try {
			importScripts('DDNet-Server.js');
		} catch (error) {
			self.postMessage({type: 'error', message: String(error)});
		}
	} else if (message.type === 'packet' && self.Module && Module.ccall && message.destination === 8303 && message.data instanceof Uint8Array && message.data.length <= 1400) {
		Module.ccall('EmscriptenLocalPacket', null, ['number', 'number', 'array', 'number'],
			[message.source, message.destination, message.data, message.data.length]);
	}
});
