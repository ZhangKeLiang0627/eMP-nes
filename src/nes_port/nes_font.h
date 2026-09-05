/*
 * Runtime font manager for eMP-nes (mirrors eMP-gba's gba_font).
 *
 * Loads SmileySans.ttf (CJK-capable) once into RAM and creates LVGL fonts
 * on demand, cached by pixel size. Uses LVGL's tiny TTF
 * (LV_USE_TINY_TTF, stb_truetype based) so no external FreeType library is
 * needed on the target. Falls back to NULL when no TTF is found - callers
 * keep the default font in that case (Chinese glyphs degrade to blanks).
 */
#ifndef NES_FONT_H
#define NES_FONT_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Get a cached TTF-derived font of `size` px, or NULL if unavailable. */
lv_font_t * nes_font_get(int size);

#ifdef __cplusplus
}
#endif

#endif /* NES_FONT_H */
