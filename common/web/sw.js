// miniwave service worker — network-first, versioned cache
const CACHE = 'miniwave-v2';

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', e => {
  // Purge old caches
  e.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  // Skip SSE and API requests
  if (e.request.url.includes('/events') || e.request.url.includes('/api')) return;

  e.respondWith(
    fetch(e.request)
      .then(r => {
        if (r.ok && e.request.method === 'GET') {
          const clone = r.clone();
          caches.open(CACHE).then(c => c.put(e.request, clone));
        }
        return r;
      })
      .catch(() => caches.match(e.request))
  );
});
