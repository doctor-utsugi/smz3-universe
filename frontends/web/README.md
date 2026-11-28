# Browser version

The PC app compiled to WebAssembly with Emscripten: the same sources as `frontends/pc` (built by its Makefile with
`WEB=1`), snes9x, the generators, Map Rando, the tracker and the online modes, in a browser tab. The first web build and
the Map Rando fix for 32-bit hosts came from Dan (danisreallycool) in pull request #1.

## Build

```
frontends/web/build.sh
```

It builds `frontends/web/Dockerfile` (Emscripten, Pillow, Rust 1.90.0 with std's sources) and runs
`make -C frontends/pc WEB=1 web` in it. It needs what `./build-pc.sh` needs, and Map Rando is built in when
`tools/maprando/fetch.sh` has fetched its sources. Output in `build/web`:

| file | what |
|---|---|
| `index.html` | the page, with the build's id in every link |
| `smz3.js`, `smz3.wasm` | the program |
| `sprites.js`, `sprites.data` | the community sprites (about 74 MB) |
| `maprando.js`, `maprando.data` | Map Rando's data and the seeds the game comes with |

The packages are kept in the browser's cache (IndexedDB) after the first visit.

## What differs from the PC app

Everything is in `__EMSCRIPTEN__` blocks of `frontends/pc` or in this folder; the PC build is unchanged.

- ROMs: the player adds their own (Add ROMs, Browse in the welcome screen, or dropping them on the page). They go to
  `/incoming`, which the menu takes like a dropped file, and stay in this browser. No ROM is ever sent or served.
- Store: `XDG_DATA_HOME` is `/data`, an IndexedDB folder (`pre.js`), written every 10 s, after each run and when the tab
  is hidden. The page asks the browser to keep it (`navigator.storage.persist`). Export saves downloads a `.tar` of the
  store without the base ROMs; adding that `.tar` in another browser brings the runs over.
- HTTP: `web.c` sends the online requests with a synchronous XMLHttpRequest on the thread that makes them, in place of
  curl. Served from the game's server, the page and the API share one origin, so no CORS is involved. RomM sync works
  only with a RomM that sends CORS headers for the page's origin.
- Sound: SDL's browser backend has one output device and the app opens three (menu music, menu sounds, the game);
  `web_audio.c` mixes them into one.
- Map Rando: built in the browser where there is memory for it (about 780 MB); phones and tablets, and desktops that
  report less than 2 GB, get the seed from the server as a BPS patch, like the 3DS.
- No update check: the page is always the newest build. One window: the dual-screen mode does not exist here.

## Serving it

Threads need `SharedArrayBuffer`, which browsers allow only on a cross-origin isolated page over HTTPS. The game's
server already has HTTPS (Caddy with a Let's Encrypt certificate for its DuckDNS name); this goes in its HTTPS site
block, before the proxy to the ladder server:

```
redir /play /play/
handle_path /play/* {
	root * /srv/smz3/web
	header Cross-Origin-Opener-Policy same-origin
	header Cross-Origin-Embedder-Policy require-corp
	header Cross-Origin-Resource-Policy same-origin
	header X-Content-Type-Options nosniff
	@page path / /index.html
	header @page Cache-Control no-cache
	header ?Cache-Control "public, max-age=31536000, immutable"
	file_server {
		precompressed br gzip
	}
}
```

The plain HTTP block, kept for the 3DS, gets `redir /play* https://{host}{uri}`. The page links every file with the
build's `?v=`, so those files can be cached for a year; the page itself is checked on every visit.

`tools/web-deploy.sh` (with `SMZ3_WEB_HOST=user@server`) compresses the files and swaps them in at once.

## Not checked yet

Gamepads (SDL uses the browser's Gamepad API), Firefox and Safari, a whole ladder or daily run, a race, and Map Rando's
memory limit on real devices.
