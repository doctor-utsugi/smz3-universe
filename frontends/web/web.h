/* The browser build's stand-ins for what the PC frontend does with programs and files (web.c). */
#ifndef WEB_H
#define WEB_H
#include <stddef.h>
#include <stdint.h>

/* HTTP status (-1 when the request failed); *out is the body, NUL-terminated, for the caller to free. want names one
 * reply header to copy into want_out (NULL for none). */
int web_http(const char *method, const char *url, const char *headers, const uint8_t *body, size_t len,
             uint8_t **out, size_t *out_len, const char *want, char *want_out, size_t want_cap);
/* "MemTotal" or "MemAvailable" in MB, as pc_maprando.c reads them from /proc/meminfo */
long web_meminfo_mb(const char *key);
/* opens the page's "Add ROMs" picker; the files land in WEB_INCOMING */
void web_pick_files(void);
/* writes the store to IndexedDB now, as after a save */
void web_store_save(void);
#define WEB_INCOMING "/incoming"
#endif
