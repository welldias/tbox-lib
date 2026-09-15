#ifndef TBOX_FONT_H
#define TBOX_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fonte/Texto is a cross-cutting service, not a pipeline stage: Layout Tree
 * depends on it to measure text, Output Display depends on it to rasterize
 * glyphs (see ARCHITECTURE.md's "Fonte / Texto" section). Unlike Style/
 * Layout/Render's per-frame-arena structs, tbox_font_source and
 * tbox_font_face use their OWN explicit lifetime (real _destroy functions,
 * no caller arena) -- they are loaded once and persist across many frames,
 * matching ARCHITECTURE.md's "Convenções" first ownership pattern, not its
 * second one. */

/* An abstract request for a font: a generic family (v0 only understands
 * generics such as "sans-serif", never a specific font name) plus
 * bold/italic flags. What a tbox_font_source backend resolves against. */
typedef struct tbox_font_query {
    tbox_string_view family;
    bool bold;
    bool italic;
} tbox_font_query;

/* Opaque: font discovery. Where the bytes of a usable font file come from,
 * given an abstract tbox_font_query -- this is where OS portability lives;
 * the rest of this layer (face/metrics/rasterization, via FreeType) is
 * already cross-platform. One backend per discovery mechanism, dispatched
 * internally through a function-pointer vtable (see src/font/tbox_font_source.c)
 * since every backend implements resolve/destroy differently. */
typedef struct tbox_font_source tbox_font_source;

/* Resolves `query` to the bytes of a usable font file. Always a blob already
 * in memory, never a path: each backend turns its own form of discovery
 * (a file path from Fontconfig, bytes handed to it directly, an OS handle in
 * a future backend, ...) into bytes internally before returning, so
 * tbox_font_face_load has exactly one form of input to accept regardless of
 * which source produced it. On success, writes the font data's address and
 * size to *out_data / *out_size (both must be non-NULL) and returns true; the
 * returned pointer is owned by `source` and remains valid until either the
 * next call to tbox_font_source_resolve on this SAME source (a backend may
 * replace its buffer to resolve a different query) or tbox_font_source_destroy,
 * whichever comes first -- v0 only ever resolves once per document (a single
 * shared tbox_font_face, see ARCHITECTURE.md), so this doesn't come up in
 * practice, but don't hold the pointer across a second resolve() call.
 * Returns false, leaving *out_data / *out_size untouched, if nothing matches
 * `query` -- the caller falls back to some other source itself (e.g. the
 * embedded backend) rather than this function trying any fallback of its
 * own. */
bool tbox_font_source_resolve(tbox_font_source *source, tbox_font_query query, const void **out_data, size_t *out_size);

/* Frees `source` and anything it owns (e.g. file bytes it read into
 * memory). A no-op if source == NULL. */
void tbox_font_source_destroy(tbox_font_source *source);

/* Fontconfig-backed source -- v0, Linux. Calls FcFontMatch/FcPatternGetString
 * to resolve `query` to a font file path, then reads that file's full
 * contents into a buffer this source owns (per tbox_font_source_resolve's
 * contract: bytes, never a path). Returns NULL only on allocation failure;
 * a missing/misconfigured Fontconfig is instead reflected by
 * tbox_font_source_resolve later returning false. */
tbox_font_source *tbox_font_source_fontconfig_create(void);

/* Embedded source -- v0, also used outside of tests as a fallback. Stores
 * `font_data`/`size` (copied into memory this source owns -- the caller's
 * buffer need not outlive this call) and always resolves to those same
 * bytes, ignoring `query` entirely. Exists primarily so tbox's automated
 * text-measurement/layout tests have a font that is always present and
 * always the same file, independent of what (if anything) Fontconfig
 * resolves on the machine running the build; see
 * external/liberation-sans/README.md for the vendored font this is meant to
 * be constructed with. Returns NULL only on allocation failure. */
tbox_font_source *tbox_font_source_embedded_create(const void *font_data, size_t size);

/* Opaque: a font face loaded to one pixel size (FT_Face + the active size,
 * see src/font/tbox_font_face.c) -- agnostic of where its bytes came from,
 * it never touches tbox_font_source itself. v0 scope: a single
 * tbox_font_face is used for the entire document (16px, the CSS2.1 initial
 * `font-size` value), not one per element/tag. */
typedef struct tbox_font_face tbox_font_face;

/* Loads a font face from `font_data`/`size` (a font file's bytes, e.g. from
 * tbox_font_source_resolve) at `size_px` pixels (FT_Set_Pixel_Sizes).
 * `font_data` is copied into memory `face` owns before FreeType ever sees
 * it -- the caller's buffer (and, if it came from a tbox_font_source, that
 * source itself) need not outlive this call. This matters because FreeType
 * itself does NOT copy the memory it's given (FT_New_Memory_Face keeps a
 * pointer and reads from it lazily, e.g. cmap parsing on first use, well
 * after this call would otherwise have returned) -- tbox_font_face_load
 * copies defensively so every caller doesn't have to reason about that.
 * Returns NULL if FreeType fails to initialize its library, parse
 * `font_data` as a font, or set the requested pixel size. */
tbox_font_face *tbox_font_face_load(const void *font_data, size_t size, double size_px);

/* Frees `face` and its underlying FT_Face. A no-op if face == NULL. */
void tbox_font_face_destroy(tbox_font_face *face);

/* The recommended distance between two consecutive baselines at `face`'s
 * loaded pixel size (FreeType's face->size->metrics.height, itself
 * ascender - descender + line gap, already scaled to the active size and
 * converted out of 26.6 fixed-point) -- what Layout uses as a text box's
 * height. */
double tbox_font_face_line_height(const tbox_font_face *face);

/* The distance from the top of a text line box down to its baseline
 * (FreeType's face->size->metrics.ascender, scaled to the active pixel size
 * and converted out of 26.6 fixed-point, same as tbox_font_face_line_height
 * above) -- what Output Display needs to turn a TEXT_RUN paint op's origin
 * (the content box's top-left corner) into a baseline `pen_y` before placing
 * each glyph via its bearing_y (see tbox_font_glyph_bitmap below, which is
 * baseline-relative, not line-top-relative). */
double tbox_font_face_ascent(const tbox_font_face *face);

/* v0: the sum of each codepoint's advance width, decoded from `text` as
 * UTF-8, with no kerning and no shaping (no ligatures, no bidi/complex
 * reordering) -- enough for simple single-line Latin text. Real shaping
 * (HarfBuzz) is a future upgrade to this SAME signature's implementation,
 * not a new layer -- it only matters once multilingual/complex text does.
 * text.size == 0 returns 0. */
double tbox_font_measure_text(const tbox_font_face *face, tbox_string_view text);

/* An 8-bit alpha coverage bitmap for one rasterized glyph (FT_Render_Glyph,
 * antialiased 8-bit mode), plus the metrics needed to place it: bearing_x/
 * bearing_y are the offset from the pen position to the bitmap's top-left
 * corner, advance is how far the pen moves for this glyph (same unit/value
 * as one term of tbox_font_measure_text's sum). width == 0 || height == 0
 * means the glyph has no visible coverage (e.g. ' ') -- alpha is NULL in
 * that case. */
typedef struct tbox_font_glyph_bitmap {
    int width, height, bearing_x, bearing_y;
    double advance;
    const unsigned char *alpha; /* width*height bytes of 8-bit coverage */
} tbox_font_glyph_bitmap;

/* Rasterizes `codepoint` at `face`'s loaded pixel size. IMPORTANT aliasing
 * note: the returned `alpha` pointer is NOT owned by the caller -- it
 * aliases FreeType's internal glyph slot buffer for `face`, which the next
 * call to tbox_font_rasterize_glyph (or any other FT_Load_char/FT_Render_Glyph
 * call) on this SAME face overwrites in place. Consume one glyph's bitmap
 * (e.g. composite it into a pixel buffer) before rasterizing the next one;
 * never hold onto `alpha` across two rasterize calls. */
tbox_font_glyph_bitmap tbox_font_rasterize_glyph(tbox_font_face *face, uint32_t codepoint);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_FONT_H */
