/* alttpr's texts: Support/Dialog.php's encoder, Text.php's table and Support/Credits.php's ending sequence, plus
   World::$texts / $credits (what a seed changes in them). Data from tools/z3/gen_z3_text.py (z3_text_gen.h). */
#ifndef Z3_TEXT_H
#define Z3_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include "z3_text_gen.h"

#define Z3_TEXT_MAX 0x7355       /* Text::getByteArray throws "Too BIG" past this */
#define Z3_DIALOG_MAX 2046       /* convertDialogCompressed's default max_bytes */

/* Credits::$scenes in order; each scene's line 0 is the one updateCreditLine changes. */
enum {
    Z3_CREDIT_castle, Z3_CREDIT_sanctuary, Z3_CREDIT_kakariko, Z3_CREDIT_desert, Z3_CREDIT_hera, Z3_CREDIT_house,
    Z3_CREDIT_zora, Z3_CREDIT_witch, Z3_CREDIT_lumberjacks, Z3_CREDIT_grove, Z3_CREDIT_well, Z3_CREDIT_smithy,
    Z3_CREDIT_kakariko2, Z3_CREDIT_bridge, Z3_CREDIT_woods, Z3_CREDIT_pedestal, Z3_CREDIT_COUNT
};

/* World::setText / setCredit values (UTF-8), last one wins per key; offsets into pool, -1 = unchanged. */
typedef struct {
    int16_t text[Z3_TXT_COUNT];
    int16_t credit[Z3_CREDIT_COUNT];
    char pool[12288];
    int used;
} Z3Texts;

void z3_texts_init(Z3Texts *t);
/* false when the pool is full (the text is then left as it was). */
bool z3_texts_set(Z3Texts *t, int key, const char *s);
bool z3_texts_credit(Z3Texts *t, int scene, const char *s);

/* Dialog::convertDialogCompressed($s, $pause) with max_bytes 2046 and wrap 19; out holds Z3_DIALOG_MAX bytes. */
int z3_dialog_encode(const char *s, bool pause, uint8_t *out);
/* Text::getByteArray() after every setString in t: the bytes Rom::writeText puts at 0xE0000. Returns the length, or
   -1 when the table outgrows Z3_TEXT_MAX (alttpr throws). out holds Z3_TEXT_MAX bytes. */
int z3_text_table(const Z3Texts *t, uint8_t *out);
/* Credits::getBinaryData() after every updateCreditLine in t: data (returns its length, at most cap) and the
   Z3_CREDIT_COUNT + 1 scene offsets Rom::writeCredits packs as 16-bit words. */
int z3_credits_data(const Z3Texts *t, uint8_t *data, int cap, uint16_t ptrs[Z3_CREDIT_COUNT + 1]);

#endif
