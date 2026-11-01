/* The help bar and button prompts: every control is drawn as the button the player actually has (keyboard key, Xbox, PlayStation or
 * Nintendo face button), following whichever device was used last. */
#ifndef PC_HELP_H
#define PC_HELP_H

#include <SDL.h>

/* pad: A B X Y L R LT RT SELECT START DPAD UD LR UP DOWN LEFT RIGHT (SDL positions: A is the bottom face button, B the right one).
 * kb: the key shown for the keyboard ("Enter", "Esc", "Q", "<", ">", "^", "v", ...). */
typedef struct { const char *pad, *kb, *label; } HelpItem;

void help_note_keyboard(void);
void help_note_pad(SDL_GameController *c);
int help_pad_in_use(void);

/* The button of a pad key (or the keyboard key), drawn centred on y with the given height; returns its width. */
float help_icon(float x, float y, float h, const char *pad, const char *kb);
/* An opaque bar over x..x+w, y..y+h with the items centred in it. */
void help_bar(float x, float y, float w, float h, const HelpItem *items, int n);
/* The bar for a shared-menu screen (320 x 240 logical), built from the hint text the core drew ("A choose  B back"). */
void help_bar_hint(float k, float ox, float oy, const char *hint);
/* True when the text starts with a button name, so it is a hint line ("A choose  B back"). */
int help_is_hint(const char *text);
/* A prompt line with inline buttons: "<A> leave and save   <B> stay". */
void help_prompt(float cx, float y, float h, const HelpItem *items, int n);

/* A list of controls for the help page: one row per item the current device has (button, then its action); an item whose
 * label starts with '#' is a section heading. Draws from y down and returns the height used (draw = 0 only measures). */
float help_list(float x, float y, float row_h, const HelpItem *items, int n, int draw);

#endif
