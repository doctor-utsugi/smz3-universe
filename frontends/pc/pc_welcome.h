/* The first screen of the PC app: two ROM cards (Super Metroid, A Link to the Past) that fill in as the files are dropped or browsed for,
 * each showing the exact ROM it needs and whether the checksum matches, then a Play! panel that lights up with a burst of sparkles.
 * Also the fade the other screens use when they change. */
#ifndef PC_WELCOME_H
#define PC_WELCOME_H

#include <SDL.h>
#include "smz3_ui.h"

typedef struct {
    const char *(*setup)(const char *path);   /* keeps the ROMs found at a path; reports each judged file through welcome_report */
    int (*ready)(void);                       /* both base ROMs are kept in the data folder */
    void (*browse)(void);                     /* opens the file browser */
    void (*existing)(char names[2][128], char md5[2][33]);   /* fills the cards from the kept copies; returns nothing when there are none */
} WelcomeHooks;

void welcome_init(const WelcomeHooks *hooks);
int welcome_active(void);
/* Shows the screen. With ROMs already kept it opens ready, otherwise empty. */
void welcome_open(void);
/* One judged file: kind as smz3_romsetup.h, md5 of the image, kept = 1 when it was copied into the data folder. */
void welcome_report(const char *source, int kind, const char *md5, int kept);
/* Files were added (drop or browser): re-reads the state and celebrates when both ROMs are now there. */
void welcome_added(void);
void welcome_nav(UiButton b);
void welcome_click(int x, int y);

/* Draws in a 320 x 240 area scaled by k at (ox, oy). */
void welcome_draw(float k, float ox, float oy);
/* The black fade that softens a change of screen: call when the screen changed, and draw every frame over everything. */
void fx_screen_changed(void);
/* 1 while a screen change still covers most of the new screen: menu input waits for it. */
int fx_busy(void);
void fx_draw(int win_w, int win_h);

#endif
