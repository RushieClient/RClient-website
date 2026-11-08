// GitHub Pages cannot set the isolation headers required by WebAssembly threads.
if (typeof window === 'undefined') {
	self.addEventListener('install', () => self.skipWaiting());
	self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
	self.addEventListener('fetch', event => {
		if (new URL(event.request.url).origin !== self.location.origin ||
			(event.request.cache === 'only-if-cached' && event.request.mode !== 'same-origin')) {
			return;
		}
		event.respondWith(fetch(event.request).then(response => {
			if (response.status === 0) {
				return response;
			}
			const headers = new Headers(response.headers);
			headers.set('Cross-Origin-Opener-Policy', 'same-origin');
			headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
			return new Response(response.body, {
				status: response.status,
				statusText: response.statusText,
				headers
			});
		}));
	});
} else {
	const workerUrl = document.currentScript.src;
	const reloadKey = 'rclient-isolation-reload';
	(async () => {
		if (!window.crossOriginIsolated) {
			if (!window.isSecureContext || !('serviceWorker' in navigator)) {
				throw new Error('This browser cannot enable WebAssembly threads. Use a current browser over HTTPS.');
			}
			if (sessionStorage.getItem(reloadKey)) {
				sessionStorage.removeItem(reloadKey);
				throw new Error('Could not enable WebAssembly threads. Reload the page to try again.');
			}
			let timer;
			try {
				await Promise.race([
					(async () => {
						await navigator.serviceWorker.register(workerUrl, {updateViaCache: 'none'});
						await navigator.serviceWorker.ready;
						if (!navigator.serviceWorker.controller) {
							await new Promise(resolve => {
								navigator.serviceWorker.addEventListener('controllerchange', resolve, {once: true});
							});
						}
					})(),
					new Promise((_, reject) => {
						timer = setTimeout(() => reject(new Error('Browser setup timed out. Reload the page.')), 20000);
					})
				]);
			} finally {
				clearTimeout(timer);
			}
			sessionStorage.setItem(reloadKey, launchState === 'waiting' ? 'start' : 'reload');
			location.reload();
			return;
		}
		if (sessionStorage.getItem(reloadKey) === 'start') {
			launchButton.click();
		}
		sessionStorage.removeItem(reloadKey);
		const script = document.createElement('script');
		script.src = new URL('DDNet.js', workerUrl).href;
		script.onerror = () => showLaunchError('Could not load client files. Reload the page.');
		document.body.appendChild(script);
	})().catch(error => {
		console.error(error);
		showLaunchError(error.message);
	});
}
