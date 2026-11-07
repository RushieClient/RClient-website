// The client and server have separate WASM memories and exchange loopback datagrams.
Module['localServer'] = {
	state: 0, // stopped, loading, ready; -1 means failed
	error: '',
	worker: null,
	timer: null,
	stopping: Promise.resolve(),
	generation: 0,
	closed: false,
	stop() {
		++this.generation;
		clearTimeout(this.timer);
		this.timer = null;
		const worker = this.worker;
		this.worker = null;
		this.state = 0;
		if (Module.calledRun && !this.closed)
			Module.ccall('EmscriptenLocalReset', null, [], []);
		if (worker) {
			this.stopping = new Promise(resolve => {
				const timeout = setTimeout(() => {
					worker.terminate();
					resolve();
				}, 2000);
				worker.addEventListener('message', event => {
					if (event.data.type === 'stopped') {
						clearTimeout(timeout);
						worker.terminate();
						resolve();
					}
				});
				worker.postMessage({type: 'stop'});
			});
		}
	},
	fail(message) {
		this.stop();
		this.error = message;
		this.state = -1;
	},
	start(args) {
		this.stop();
		this.error = '';
		this.state = 1;
		const pending = this.stopping;
		const generation = this.generation;
		pending.then(() => {
			if (this.state !== 1 || this.generation !== generation)
				return;
			let worker;
			try {
				worker = new Worker('local_server_worker.js');
			} catch (error) {
				this.fail(String(error));
				return;
			}
			this.worker = worker;
			this.timer = setTimeout(() => {
				if (this.worker === worker)
					this.fail('Local Tutorial server did not start within 60 seconds.');
			}, 60000);
			worker.addEventListener('error', event => {
				event.preventDefault();
				if (this.worker === worker)
					this.fail(event.message || 'Failed to load local Tutorial server.');
			});
			worker.addEventListener('message', event => {
				if (this.worker !== worker)
					return;
				const message = event.data;
				if (message.type === 'ready') {
					clearTimeout(this.timer);
					this.state = 2;
				} else if (message.type === 'error') {
					this.fail(message.message);
				} else if (message.type === 'packet' && message.source === 8303 && message.data instanceof Uint8Array && message.data.length <= 1400) {
					Module.ccall('EmscriptenLocalPacket', null, ['number', 'number', 'array', 'number'],
						[message.source, message.destination, message.data, message.data.length]);
				}
			});
			worker.postMessage({type: 'start', args});
		});
	}
};

Module['localNetworkSend'] = (source, destination, data) => {
	const server = Module['localServer'];
	if (server.state === 2 && server.worker && destination === 8303)
		server.worker.postMessage({type: 'packet', source, destination, data}, [data.buffer]);
};

window.addEventListener('pagehide', () => Module['localServer'].stop());
