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

/* An abstract request for a font: a family (a generic alias such as
 * "sans-serif"/"serif"/"monospace", or a specific font name such as
 * "Verdana" -- both are arbitrary strings as far as this struct and
 * tbox_font_source are concerned; which ones actually resolve to something
 * is entirely up to the backend, e.g. Fontconfig's own alias/substitution
 * rules) plus bold/italic flags. What a tbox_font_source backend resolves
 * against. */
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

/* True when the face contains a drawable glyph for `codepoint` (not the
 * font's missing-glyph replacement). Useful before selecting a font for
 * symbols such as U+2713 CHECK MARK. */
bool tbox_font_face_has_glyph(const tbox_font_face *face, uint32_t codepoint);

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

/* Opaque: owns two copies of font bytes (regular and bold -- see "why two
 * font sources" below, in tbox_font_face_cache_create's doc comment) plus a
 * vector of already-loaded tbox_font_face instances, keyed by (family, bold,
 * size_px), populated on demand. Why a cache and not just a second face:
 * tbox_font_face_load already bakes the pixel size into the load
 * (FT_Set_Pixel_Sizes) -- a loaded face can't be rescaled. Once font-size
 * varies per element (headings vs. body text, and whatever an author
 * declares), loading one face per (family, weight, size) combination on
 * demand, with a cache, is the only way to support that without re-parsing
 * the font file for every text box on every frame. Families other than the
 * default (empty) one are resolved on demand via the resolver callback
 * passed to tbox_font_face_cache_create -- see that function's doc comment.
 * Own lifetime (real _destroy, no caller arena) -- same pattern as
 * tbox_font_face itself: loaded once, persists across many frames, NOT
 * reset by tbox_context's frame arena. */
typedef struct tbox_font_face_cache tbox_font_face_cache;

/* Resolves an arbitrary font family (plus bold/italic) to the bytes of a
 * usable font file, on demand, for tbox_font_face_cache_get -- same success/
 * failure contract as tbox_font_source_resolve: on success, writes the font
 * data's address and size to *out_data / *out_size (both non-NULL) and
 * returns true; on failure, returns false and leaves *out_data / *out_size
 * untouched. `userdata` is whatever opaque pointer was passed alongside this
 * function to tbox_font_face_cache_create, unused by the cache itself.
 * Deliberately a callback rather than this header (or tbox_font_face_cache.c)
 * calling a specific backend such as tbox_font_source_fontconfig_* directly:
 * it keeps src/font/tbox_font_face_cache.c free of any dependency on a
 * specific backend, same as it is today -- the Application layer is the one
 * that knows Fontconfig exists and supplies a resolver built on top of it
 * (see ARCHITECTURE.md's "v12 -- CSS font-family" -> "Escopo"). The returned
 * pointer's lifetime follows whatever the backing tbox_font_source behind
 * this callback documents (e.g. tbox_font_source_resolve's own aliasing
 * rules) -- tbox_font_face_cache_get copies the bytes into its own arena
 * immediately, so the caller of this typedef's implementation need not keep
 * them valid past the call. */
typedef bool (*tbox_font_resolver_fn)(void *userdata, tbox_font_query query, const void **out_data, size_t *out_size);

/* Copies regular_data/bold_data into memory the cache owns -- the caller's
 * buffers need not outlive this call (same defensive copy
 * tbox_font_face_load already does, just done once per variant here, since
 * every size_px requested later needs the original bytes again for a fresh
 * tbox_font_face_load). Why two already-resolved byte blobs, rather than
 * this function resolving a tbox_font_query itself: tbox_font_source_resolve's
 * contract documents that its returned pointer stays valid only until the
 * NEXT resolve() call on that SAME source -- resolving {bold:false} and then
 * {bold:true} on one source would invalidate the first result before the
 * caller could hand both to this function. Building the cache is therefore
 * the caller's job (the Application layer): create two
 * tbox_font_source_fontconfig instances, one per query, resolve each exactly
 * once, and pass both (data, size) pairs here -- this cache only receives
 * bytes already in hand, it never touches tbox_font_source itself.
 * `resolver`/`resolver_userdata` are stored on the cache and used by
 * tbox_font_face_cache_get to resolve any family OTHER than the default
 * (empty) one, the first time that (family, bold) combination is requested
 * -- see tbox_font_face_cache_get's doc comment. `resolver` may be NULL
 * (e.g. a test build with no backend available): any non-default family
 * then always fails to resolve, same failure contract
 * tbox_font_face_cache_get already has for a load failure. Returns NULL
 * only on allocation failure. */
tbox_font_face_cache *tbox_font_face_cache_create(const void *regular_data, size_t regular_size, const void *bold_data, size_t bold_size, tbox_font_resolver_fn resolver, void *resolver_userdata);

/* Frees every tbox_font_face this cache loaded, plus the cache's own copied
 * byte buffers and the cache struct itself. A no-op if cache == NULL. */
void tbox_font_face_cache_destroy(tbox_font_face_cache *cache);

/* Looks up (family, bold, italic, size_px) in the cache; on a miss, calls
 * tbox_font_face_load internally and stores the result before returning it
 * -- same arena/malloc-backed vector-plus-linear-scan idiom already used by
 * tbox_style_table and v1's click-handler table, just with lazy loading
 * instead of everything pre-populated up front.
 *
 * `family.size == 0` (the empty/default family) with `italic == false` uses
 * the bytes given to tbox_font_face_cache_create (regular_data/bold_data)
 * directly -- the fast path this function has always had, no resolver call.
 * `family.size == 0` with `italic == true` (NOVO v13) does NOT take that
 * fast path -- regular_data/bold_data have no italic variant of their own --
 * and instead resolves on demand exactly like any other family (see below),
 * with `family` passed to the resolver unchanged: a resolver backed by
 * Fontconfig already substitutes "sans-serif" for an empty family (see
 * tbox_font_source_fontconfig_family_cstr), the same substitution
 * regular_data/bold_data's own eager bootstrap already relies on, so this
 * needs no help from the caller. Any family (default-with-italic, or any
 * other name) is resolved on demand, once per (family, bold, italic) triple
 * no matter how many distinct size_px values are later requested for it: the
 * first time (family, bold, italic) is seen, this function checks its
 * internal family_blobs cache first, and only calls `cache`'s resolver (see
 * tbox_font_resolver_fn/tbox_font_face_cache_create) if that triple hasn't
 * been resolved before. A resolver's successful result is copied into the
 * cache's own memory and kept in family_blobs, reused by any later size_px
 * for that same (family, bold, italic) without calling the resolver again.
 * If the resolver is NULL, or it (or tbox_font_face_load) fails, nothing is
 * cached, so a later retry with the same parameters tries again rather than
 * being permanently stuck (this includes a default-family italic request on
 * a cache with no resolver configured: it now returns NULL rather than
 * silently falling back to a non-italic face). The returned pointer stays
 * valid for `cache`'s whole lifetime (never invalidated by later calls to
 * this function, unlike the aliasing warning on tbox_font_rasterize_glyph's
 * return value). */
const tbox_font_face *tbox_font_face_cache_get(tbox_font_face_cache *cache, tbox_string_view family, bool bold, bool italic, double size_px);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_FONT_H */
