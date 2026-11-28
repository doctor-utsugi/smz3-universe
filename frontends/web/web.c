/* What the browser build does in place of the PC's programs and files: HTTP without curl, the memory Map Rando may use,
 * the browser's file picker, and the store kept in IndexedDB (pre.js). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#include "web.h"

/* A synchronous XMLHttpRequest: the app's requests run on worker threads, where that is allowed. The reply stays in this
 * thread's JS global until web_http_take copies it. */
EM_JS(int, web_http_send, (const char *method, const char *url, const char *headers, const uint8_t *body, int len,
                           const char *want, int *out_len), {
  var x = new XMLHttpRequest();
  globalThis.smz3Reply = '';
  globalThis.smz3Header = '';
  try {
    x.open(UTF8ToString(method), UTF8ToString(url), false);
    x.overrideMimeType('text/plain; charset=x-user-defined');
    UTF8ToString(headers).split('\n').forEach(function (h) {
      var i = h.indexOf(':');
      if (i > 0) x.setRequestHeader(h.slice(0, i).trim(), h.slice(i + 1).trim());
    });
    x.send(len > 0 ? HEAPU8.slice(body, body + len) : null);
  } catch (e) { HEAP32[out_len >> 2] = 0; return -1; }
  globalThis.smz3Reply = x.responseText || '';
  if (want) globalThis.smz3Header = x.getResponseHeader(UTF8ToString(want)) || '';
  HEAP32[out_len >> 2] = globalThis.smz3Reply.length;
  return x.status ? x.status : -1;
});

EM_JS(void, web_origin, (char *out, int cap), { stringToUTF8(globalThis.location.origin, out, cap); });

EM_JS(void, web_http_take, (uint8_t *dst, int n, char *hdr, int hdr_cap), {
  var t = globalThis.smz3Reply;
  for (var i = 0; i < n; i++) HEAPU8[dst + i] = t.charCodeAt(i) & 255;
  if (hdr) stringToUTF8(globalThis.smz3Header, hdr, hdr_cap);
  globalThis.smz3Reply = globalThis.smz3Header = '';
});

int web_http(const char *method, const char *url, const char *headers, const uint8_t *body, size_t len,
             uint8_t **out, size_t *out_len, const char *want, char *want_out, size_t want_cap)
{
    int n = 0;
    int status = web_http_send(method, url, headers ? headers : "", body, (int)len, want, &n);
    uint8_t *o = malloc((size_t)n + 1);
    if (o) web_http_take(o, n, want ? want_out : NULL, (int)want_cap);
    else n = 0;
    if (o) o[n] = 0;
    if (out) *out = o; else free(o);
    if (out_len) *out_len = (size_t)n;
    return status;
}

/* MemTotal for Map Rando's gate (768 MB in all, 400 MB free, pc_maprando.c): phones and tablets count as 1 GB, other
 * devices report navigator.deviceMemory. Free is what is left of the 4 GB a wasm32 program can address. */
EM_JS(int, web_device_mb, (void), {
  var ua = navigator.userAgentData;
  if ((ua && ua.mobile) || /Android|iPhone|iPad|iPod|Mobile/.test(navigator.userAgent)) return 1024;
  return navigator.deviceMemory ? navigator.deviceMemory * 1024 : 8192;
});

long web_meminfo_mb(const char *key)
{
    if (!strcmp(key, "MemTotal")) return web_device_mb();
    return 4096 - (long)(emscripten_get_heap_size() >> 20);
}

/* Browsers open a file picker only shortly after a key press or click, which is when the menu calls this */
void web_pick_files(void) { MAIN_THREAD_ASYNC_EM_ASM({ var i = document.getElementById('roms'); if (i) i.click(); }); }

void web_store_save(void) { MAIN_THREAD_ASYNC_EM_ASM({ if (Module.smz3Save) Module.smz3Save(); }); }
