/* Telling the two base ROMs apart: the user hands over files in any order, with or without a copier header, loose or zipped. */
#ifndef SMZ3_ROMSETUP_H
#define SMZ3_ROMSETUP_H

#include <stddef.h>
#include <stdint.h>

enum { SMZ_ROM_UNKNOWN = 0, SMZ_ROM_SM = 1, SMZ_ROM_Z3 = 2, SMZ_ROM_SM_OTHER = 3, SMZ_ROM_Z3_OTHER = 4 };

/* Judges a ROM image by size and MD5; start/len receive the image without a 512-byte copier header. SM_OTHER and Z3_OTHER are
 * the right game in a revision the patches do not fit. */
int smz_rom_classify(const uint8_t *data, size_t n, const uint8_t **start, size_t *len);
/* A short sentence for a classification, e.g. "Super Metroid (JU) found" or why it cannot be used. */
const char *smz_rom_message(int kind);

/* True when the bytes start like a zip file. */
int smz_is_zip(const uint8_t *data, size_t n);
/* Unpacks the entry number idx (0-based, files only) of a zip into a malloc'd buffer; NULL past the last entry or on an
 * unsupported entry (only stored and deflate). */
uint8_t *smz_zip_entry(const uint8_t *zip, size_t n, int idx, size_t *out_len);

#endif
