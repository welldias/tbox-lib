# Liberation Sans

`LiberationSans-Regular.ttf`, vendored from the
[Liberation Fonts](https://github.com/liberationfonts/liberation-fonts)
project, version **2.1.5** (release tag `2.1.5`, the
`liberation-fonts-ttf-2.1.5.tar.gz` prebuilt-TTF asset attached to that
release). Only the Regular weight is vendored -- see "Only the Regular
weight" below.

Licensed under the SIL Open Font License, Version 1.1 (full text in
`LICENSE`, copied verbatim from the upstream release).

## Why this font

Used by `tbox_font_source_embedded` (Fonte/Texto layer, `src/font/`) to give
`tbox`'s automated tests a font that is always present and always the same
file, independent of what (if anything) Fontconfig resolves on the machine
running the build -- a dev workstation and a bare CI container should not
measure/rasterize text differently just because one has more system fonts
installed than the other. Liberation Sans specifically because it is
metric-compatible with Arial (predictable advances for a well-known
reference face), permissively licensed for embedding/redistribution, and
already the default sans-serif on many Linux distros -- so in practice it
also tends to match what `tbox_font_source_fontconfig` resolves for
"sans-serif" on a typical dev machine, keeping the two backends visually
consistent without that being the actual reason it was chosen.

## Only the Regular weight

The upstream release ships Regular/Bold/Italic/BoldItalic for three
families (Sans, Serif, Mono); only `LiberationSans-Regular.ttf` is
vendored here. `tbox` v0 has no `font-weight`/`font-style` support yet (one
`tbox_font_face` for the whole document, see `ARCHITECTURE.md`'s
"Fonte / Texto" section), so vendoring the other weights now would be dead
weight with no code or test exercising them. Add them if/when the Style
layer grows `font-weight`/`font-style` and `tbox_font_source_embedded`
learns to pick a weight from the query.

## Updating

1. Grab the `liberation-fonts-ttf-<version>.tar.gz` asset linked from the
   desired release's notes at
   https://github.com/liberationfonts/liberation-fonts/releases (the
   GitHub Releases API lists no `assets` for this repo -- the prebuilt TTF
   tarball is attached as a file link inside the release body instead of a
   proper release asset).
2. Replace `LiberationSans-Regular.ttf` and `LICENSE` from the extracted
   tarball.
3. Update the version above.
