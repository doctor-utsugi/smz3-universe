# Builds the PC frontend: snes9x (libretro core, vendored) as a static library, the shared C core and the SDL2 app.
ROOT := $(abspath ../..)
S9X := $(ROOT)/vendor/snes9x_libretro
CORE := $(ROOT)/core
# make WIN=1 cross-builds smz3.exe with MinGW-w64 (build-pc.sh windows prepares build/win/SDL2 and the snes9x copy)
ifdef WEB
# make WEB=1 builds the browser version with Emscripten (frontends/web/build.sh runs it in the emsdk image)
WEBDIR := $(ROOT)/frontends/web
OUT := $(ROOT)/build/web
CC := emcc
CFLAGS := -O2 -pthread -sUSE_SDL=2 -I$(CURDIR) -I$(WEBDIR) -std=gnu11 -w -I$(CORE) -I$(CORE)/gen -I$(CORE)/smgen -I$(CORE)/z3gen -I$(CORE)/ui -I$(CORE)/common -I$(S9X)/libretro -I$(S9X)/libretro/libretro-common/include -include $(WEBDIR)/web_audio.h
LIBS := -O2 -pthread -sUSE_SDL=2 -sASYNCIFY -sASYNCIFY_STACK_SIZE=262144 -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=4294967296 \
        -sSTACK_SIZE=4194304 -sDEFAULT_PTHREAD_STACK_SIZE=4194304 -sPTHREAD_POOL_SIZE=8 -sPTHREAD_POOL_SIZE_STRICT=0 -sFORCE_FILESYSTEM -lidbfs.js \
        -sEXPORTED_RUNTIME_METHODS=FS,ENV,IDBFS,addRunDependency,removeRunDependency -sENVIRONMENT=web,worker -sNO_DISABLE_EXCEPTION_CATCHING \
        --pre-js $(WEBDIR)/pre.js -lstdc++
EXE := .js
else ifdef WIN
OUT := $(ROOT)/build/win
CC := x86_64-w64-mingw32-gcc
SDL := $(OUT)/SDL2/x86_64-w64-mingw32
S9X := $(OUT)/s9x
CFLAGS := -O2 -I$(CURDIR) -std=gnu11 -w -DSDL_MAIN_HANDLED -I$(CORE) -I$(CORE)/gen -I$(CORE)/smgen -I$(CORE)/z3gen -I$(CORE)/ui -I$(CORE)/common -I$(S9X)/libretro -I$(S9X)/libretro/libretro-common/include -I$(SDL)/include/SDL2
LIBS := -static -L$(SDL)/lib -lSDL2 -lm -lstdc++ -lwinmm -limm32 -lole32 -loleaut32 -lversion -lsetupapi -lgdi32 -lcrypt32 -lshell32 -luser32 -ladvapi32 -lwinpthread
EXE := .exe
else
OUT := $(ROOT)/build/pc
CFLAGS := -O2 -I$(CURDIR) -std=gnu11 -w -I$(CORE) -I$(CORE)/gen -I$(CORE)/smgen -I$(CORE)/z3gen -I$(CORE)/ui -I$(CORE)/common -I$(S9X)/libretro -I$(S9X)/libretro/libretro-common/include $(shell sdl2-config --cflags)
ifeq ($(shell uname -s),Darwin)
# macOS: SDL2 goes inside the binary (Homebrew's libSDL2.a), so players need no Homebrew
LIBS := $(shell sdl2-config --prefix)/lib/libSDL2.a $(filter-out -lSDL2 -L%,$(shell sdl2-config --static-libs)) -lm -lstdc++
else
LIBS := $(shell sdl2-config --libs) -lm -lstdc++
endif
EXE :=
endif
# the release tag shown in the run menu: the tag CI builds, else the newest tag here
SMZ3_VERSION ?= $(or $(filter v%,$(GITHUB_REF_NAME)),$(shell git -C $(ROOT) describe --tags --abbrev=0 2>/dev/null),dev)
CFLAGS += -DSMZ3_VERSION='"$(SMZ3_VERSION)"'
CFLAGS += -ffile-prefix-map=$(HOME)=~   # no build carries the builder's home folder (__FILE__ in asserts)

# Map Rando (core/maprando, Rust) is linked in when its sources are fetched (tools/maprando/fetch.sh) and cargo is installed
MAPRANDO ?= $(if $(and $(wildcard $(ROOT)/vendor/maprando/rust/maprando),$(shell command -v cargo)),1,)
ifdef MAPRANDO
MR_TARGET := $(ROOT)/build/maprando-target
ifdef WEB
# std rebuilt with atomics: Map Rando runs on a thread over the app's shared memory
MR_TARGET := $(ROOT)/build/maprando-wasm
MR_LIB := $(MR_TARGET)/wasm32-unknown-emscripten/release/libsmz3_maprando.a
MR_CARGO := --target wasm32-unknown-emscripten -Zbuild-std=std,panic_unwind
MR_ENV := RUSTC_BOOTSTRAP=1 CARGO_TARGET_WASM32_UNKNOWN_EMSCRIPTEN_LINKER=em++ CFLAGS_wasm32_unknown_emscripten=-pthread
MR_FLAGS := -C target-feature=+atomics,+bulk-memory,+mutable-globals
else ifdef WIN
MR_LIB := $(MR_TARGET)/x86_64-pc-windows-gnu/release/libsmz3_maprando.a
MR_CARGO := --target x86_64-pc-windows-gnu
LIBS += -lws2_32 -lbcrypt -luserenv -lntdll
else
MR_LIB := $(MR_TARGET)/release/libsmz3_maprando.a
LIBS += -ldl -lpthread
endif
CFLAGS += -DSMZ_MAPRANDO -I$(CORE)/maprando
endif

SMGEN := sm_generate sm_fill sm_world sm_world_gen sm_patch sm_options_gen
Z3GEN := $(notdir $(basename $(wildcard $(CORE)/z3gen/*.c)))
GEN := smz3_generate smz3_fill smz3_world smz3_world_gen smz3_rng smz3_patch smz3_rom smz3_text smz3_sprite smz3_hints smz3_itemopts smz3_plando smz3_md5 smz3_romsetup smz3_romderived smz3_dungeonmaps smz3_goalicons
SRC := $(addprefix $(CORE)/gen/,$(addsuffix .c,$(GEN))) $(addprefix $(CORE)/smgen/,$(addsuffix .c,$(SMGEN))) $(addprefix $(CORE)/z3gen/,$(addsuffix .c,$(Z3GEN))) $(CORE)/smz3_state.c $(CORE)/smz3_locations.c $(CORE)/smz3_inventory.c \
       $(CORE)/common/smz3_bps.c $(CORE)/ui/smz3_ui.c $(CORE)/ui/smz3_ui_opts.c $(CORE)/ui/smz3_run.c $(CORE)/ui/smz3_host.c $(CORE)/ui/smz3_tracker.c $(CORE)/ui/smz3_goal.c $(CORE)/ui/smz3_daily.c $(CORE)/ui/smz3_race.c $(CORE)/ui/smz3_shortcuts.c $(CORE)/ui/smz3_counters.c $(CORE)/ui/smz3_discovery.c $(CORE)/ui/smz3_runlogic.c $(CORE)/ui/smz3_proof.c $(CORE)/ui/smz3_preview.c $(CORE)/ui/smz3_rewind.c $(CORE)/ui/smz3_sm.c $(CORE)/ui/smz3_z3.c $(CORE)/ui/smz3_smmap.c $(CORE)/ui/smz3_json.c $(CORE)/ui/smz3_romm.c $(CORE)/ui/smz3_sync.c \
       $(wildcard pc_*.c) font_data.c $(OUT)/ips_data.c $(OUT)/sm_ips_data.c $(OUT)/z3_ips_data.c $(OUT)/map_data.c $(OUT)/logo_data.c $(OUT)/music_data.c
SRC += $(CORE)/state/smz3_statecvt.c
ifdef WEB
SRC += $(WEBDIR)/web.c $(WEBDIR)/web_audio.c
endif
OBJ := $(patsubst %.c,$(OUT)/o/%.o,$(notdir $(SRC)))

all: $(OUT)/smz3$(EXE)

ifdef WEB
# -pthread: wasm shared memory needs every object built with atomics; STATIC_LINKING=0 keeps libretro-common's file streams.
# The core is cleaned before and after, so the PC build never links these objects.
$(OUT)/libsnes9x.a:
	$(MAKE) -C $(S9X)/libretro clean >/dev/null
	$(MAKE) -C $(S9X)/libretro platform=emscripten STATIC_LINKING=0 CC='emcc -pthread -O2' CXX='em++ -pthread -O2' -j$$(getconf _NPROCESSORS_ONLN) >/dev/null
	mkdir -p $(OUT)
	cd $(S9X) && emar rcs $(OUT)/libsnes9x.a $$(find . -name '*.o' ! -name 'compat_*snprintf*' | sort)
	$(MAKE) -C $(S9X)/libretro clean >/dev/null
else ifdef WIN
$(OUT)/libsnes9x.a:
	$(MAKE) -C $(S9X)/libretro platform=win CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ -j$$(getconf _NPROCESSORS_ONLN) >/dev/null
	cd $(S9X) && x86_64-w64-mingw32-ar rcs $(OUT)/libsnes9x.a $$(find . -name '*.o' ! -name 'compat_*snprintf*' | sort)
else
$(OUT)/libsnes9x.a:
	$(MAKE) -C $(S9X)/libretro platform=$(or $(S9X_PLATFORM),unix) -j$$(getconf _NPROCESSORS_ONLN) >/dev/null
	mkdir -p $(OUT)
	cd $(S9X) && ar rcs $(OUT)/libsnes9x.a $$(find . -name '*.o' ! -name 'compat_*snprintf*' | sort)
endif

$(OUT)/ips_data.c: $(ROOT)/tools/gen_ips_c.py
	mkdir -p $(OUT)
	python3 $< $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/ips $@

$(OUT)/sm_ips_data.c: $(ROOT)/tools/gen_sm_ips_c.py $(ROOT)/patches/sm_randomizer.ips
	mkdir -p $(OUT)
	python3 $< $(ROOT)/patches/sm_randomizer.ips $@

$(OUT)/z3_ips_data.c: $(ROOT)/tools/z3/gen_z3_ips_c.py $(ROOT)/patches/z3_base.ips
	mkdir -p $(OUT)
	python3 $< $(ROOT)/patches/z3_base.ips $@

$(OUT)/map_data.c: $(ROOT)/tools/gen_pc_assets.py $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/map_patches.png $(wildcard $(ROOT)/assets/flags/*.png) $(wildcard $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/goals/*.png)
	mkdir -p $(OUT)
	python3 $< $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3 $@

$(OUT)/logo_data.c: $(ROOT)/tools/gen_pc_logo.py $(ROOT)/assets/logo.png
	mkdir -p $(OUT)
	python3 $< $(ROOT)/assets/logo.png $@

$(OUT)/music_data.c: $(ROOT)/tools/gen_pc_music.py $(ROOT)/assets/music/smz3_intro.mp3
	mkdir -p $(OUT)
	python3 $< $(ROOT)/assets/music/smz3_intro.mp3 $@

VPATH := $(sort $(dir $(SRC)))
$(OUT)/o/%.o: %.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Explicit rules for the generated sources: the make shipped with macOS (3.81) does not find them through VPATH.
$(OUT)/o/ips_data.o: $(OUT)/ips_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/o/sm_ips_data.o: $(OUT)/sm_ips_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/o/z3_ips_data.o: $(OUT)/z3_ips_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/o/map_data.o: $(OUT)/map_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/smz3$(EXE): $(OBJ) $(OUT)/libsnes9x.a $(MR_LIB)
	$(CC) $(OBJ) $(OUT)/libsnes9x.a $(MR_LIB) -o $@ $(LIBS)

ifdef WEB
# The sprites and Map Rando's data are packages of their own, cached by the browser, so the program itself stays small
PACKAGER := python3 $(EMSDK)/upstream/emscripten/tools/file_packager.py
$(OUT)/sprites.data: $(wildcard $(ROOT)/assets/sprites/*/*)
	cd $(ROOT)/assets && $(PACKAGER) $@ --preload sprites@/sprites --use-preload-cache --js-output=$(OUT)/sprites.js >/dev/null
$(OUT)/maprando.data: $(wildcard $(ROOT)/assets/maprando/*) $(if $(MAPRANDO),$(wildcard $(ROOT)/build/maprando-data/rust/gamedata.bin))
	cd $(ROOT) && $(PACKAGER) $@ --preload assets/maprando@/maprando-pool $(if $(MAPRANDO),--preload build/maprando-data@/maprando-data) \
	  --use-preload-cache --js-output=$(OUT)/maprando.js >/dev/null
# the build's id in the page's links, so a browser never mixes an old smz3.js with a new smz3.wasm
$(OUT)/index.html: $(ROOT)/frontends/web/index.html $(OUT)/smz3$(EXE) $(OUT)/sprites.data $(OUT)/maprando.data
	sed "s/@BUILD@/$$(cat $(OUT)/smz3.wasm $(OUT)/smz3.js $(OUT)/sprites.js $(OUT)/maprando.js | sha1sum | cut -c1-12)/g" $< > $@
web: $(OUT)/index.html
.PHONY: web
endif

ifdef MAPRANDO
# The home folder is cut from the source paths Rust keeps for panics, so no build carries the builder's user name.
# cargo decides what to rebuild; the crate's rust-toolchain.toml pins the compiler, so it runs from the crate's folder
$(MR_LIB): FORCE
	cd $(CORE)/maprando && $(MR_ENV) RUSTFLAGS="$$RUSTFLAGS $(MR_FLAGS) --remap-path-prefix=$(HOME)=~" cargo build --release --lib $(MR_CARGO) --target-dir $(MR_TARGET)
FORCE:
endif

-include $(OBJ:.o=.d)

clean:
	rm -rf $(OUT)
.PHONY: all clean
$(OUT)/o/logo_data.o: $(OUT)/logo_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/o/music_data.o: $(OUT)/music_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
