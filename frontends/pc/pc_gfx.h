/* Drawing helpers on an SDL renderer: the embedded bitmap font, rectangles, and the shared menu's command list. */
#ifndef PC_GFX_H
#define PC_GFX_H

#include <SDL.h>
#include <stddef.h>
#include <stdint.h>
#include "smz3_ui.h"

void gfx_init(SDL_Renderer *r);
/* Draws on [r] from now on: the first renderer is gfx_init's, a second one (the tracker's screen in dual-screen mode) gets
 * its own copy of every texture. gfx_slot() is 0 or 1, for modules that keep textures of their own. */
void gfx_use(SDL_Renderer *r);
int gfx_slot(void);
/* Text with its top-left corner at x, y; px is the character height. align 0 left, 1 centre, 2 right. */
void gfx_text(float x, float y, float px, SDL_Color c, int align, const char *s);
float gfx_text_width(float px, const char *s);
void gfx_rect_idx(float k, float ox, float oy, float x, float y, float w, float h, int color);
void gfx_backdrop_ui(float k, float ox, float oy);
void gfx_quad(const float *xy, const SDL_Color *c);   /* four corners, each with its own colour */
void gfx_chamfer(float x, float y, float w, float h, float cut, SDL_Color c);   /* a box with its corners cut off */
void gfx_logo(float x, float y, float size, float alpha);
void gfx_rect(float x, float y, float w, float h, SDL_Color c);
SDL_Color gfx_ui_color(int index);
/* Draws the menu's last ui_draw command list scaled to fit the area, centred. Returns the scale (window px per canvas px)
 * and the origin through ox, oy. */
/* Gives the loader of sprite previews: key "link/<name>" or "samus/<name>" -> the bytes of its .pv file (malloc'd) or NULL. */
void gfx_set_preview_loader(uint8_t *(*load)(const char *key, size_t *len));
float gfx_ui(const SmzUi *u, int win_w, int win_h, float *ox, float *oy);

#endif
