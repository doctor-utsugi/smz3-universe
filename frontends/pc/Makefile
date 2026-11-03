# Builds the PC frontend: snes9x (libretro core, vendored) as a static library, the shared C core and the SDL2 app.
ROOT := $(abspath ../..)
S9X := $(ROOT)/vendor/snes9x_libretro
CORE := $(ROOT)/core
# make WIN=1 cross-builds smz3.exe with MinGW-w64 (build-pc.sh windows prepares build/win/SDL2 and the snes9x copy)
ifdef WIN
OUT := $(ROOT)/build/win
CC := x86_64-w64-mingw32-gcc
SDL := $(OUT)/SDL2/x86_64-w64-mingw32
S9X := $(OUT)/s9x
CFLAGS := -O2 -I$(CURDIR) -std=gnu11 -w -DSDL_MAIN_HANDLED -I$(CORE) -I$(CORE)/gen -I$(CORE)/ui -I$(S9X)/libretro -I$(S9X)/libretro/libretro-common/include -I$(SDL)/include/SDL2
LIBS := -static -L$(SDL)/lib -lSDL2 -lm -lstdc++ -lwinmm -limm32 -lole32 -loleaut32 -lversion -lsetupapi -lgdi32 -lcrypt32 -lshell32 -luser32 -ladvapi32 -lwinpthread
EXE := .exe
else
OUT := $(ROOT)/build/pc
CFLAGS := -O2 -I$(CURDIR) -std=gnu11 -w -I$(CORE) -I$(CORE)/gen -I$(CORE)/ui -I$(S9X)/libretro -I$(S9X)/libretro/libretro-common/include $(shell sdl2-config --cflags)
ifeq ($(shell uname -s),Darwin)
# macOS: SDL2 goes inside the binary (Homebrew's libSDL2.a), so players need no Homebrew
LIBS := $(shell sdl2-config --prefix)/lib/libSDL2.a $(filter-out -lSDL2 -L%,$(shell sdl2-config --static-libs)) -lm -lstdc++
else
LIBS := $(shell sdl2-config --libs) -lm -lstdc++
endif
EXE :=
endif

GEN := smz3_generate smz3_fill smz3_world smz3_world_gen smz3_rng smz3_patch smz3_rom smz3_text smz3_sprite smz3_hints smz3_itemopts smz3_plando smz3_md5 smz3_romsetup smz3_romderived smz3_dungeonmaps smz3_goalicons
SRC := $(addprefix $(CORE)/gen/,$(addsuffix .c,$(GEN))) $(CORE)/smz3_state.c $(CORE)/smz3_locations.c $(CORE)/smz3_inventory.c \
       $(CORE)/ui/smz3_ui.c $(CORE)/ui/smz3_ui_opts.c $(CORE)/ui/smz3_run.c $(CORE)/ui/smz3_host.c $(CORE)/ui/smz3_tracker.c $(CORE)/ui/smz3_goal.c $(CORE)/ui/smz3_daily.c $(CORE)/ui/smz3_shortcuts.c $(CORE)/ui/smz3_counters.c $(CORE)/ui/smz3_discovery.c $(CORE)/ui/smz3_runlogic.c $(CORE)/ui/smz3_proof.c $(CORE)/ui/smz3_preview.c $(CORE)/ui/smz3_rewind.c \
       $(wildcard pc_*.c) font_data.c $(OUT)/ips_data.c $(OUT)/map_data.c $(OUT)/logo_data.c
OBJ := $(patsubst %.c,$(OUT)/o/%.o,$(notdir $(SRC)))

all: $(OUT)/smz3$(EXE)

ifdef WIN
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

$(OUT)/map_data.c: $(ROOT)/tools/gen_pc_assets.py $(wildcard $(ROOT)/assets/flags/*.png) $(wildcard $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3/goals/*.png)
	mkdir -p $(OUT)
	python3 $< $(ROOT)/vendor/Lemuroid/lemuroid-app/src/main/assets/smz3 $@

$(OUT)/logo_data.c: $(ROOT)/tools/gen_pc_logo.py $(ROOT)/assets/logo.png
	mkdir -p $(OUT)
	python3 $< $(ROOT)/assets/logo.png $@

VPATH := $(sort $(dir $(SRC)))
$(OUT)/o/%.o: %.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Explicit rules for the generated sources: the make shipped with macOS (3.81) does not find them through VPATH.
$(OUT)/o/ips_data.o: $(OUT)/ips_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/o/map_data.o: $(OUT)/map_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/smz3$(EXE): $(OBJ) $(OUT)/libsnes9x.a
	$(CC) $(OBJ) $(OUT)/libsnes9x.a -o $@ $(LIBS)

-include $(OBJ:.o=.d)

clean:
	rm -rf $(OUT)
.PHONY: all clean
$(OUT)/o/logo_data.o: $(OUT)/logo_data.c
	@mkdir -p $(OUT)/o
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
