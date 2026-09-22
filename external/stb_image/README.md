# stb_image.h / stb_image_resize2.h

Vendored from https://github.com/nothings/stb, two single-header
public-domain/MIT libraries from the same project:

- `stb_image.h` (v2.30) -- image decoder (PNG/JPEG/BMP/GIF/...). Used by
  `src/image/tbox_image.c` (`#define STB_IMAGE_IMPLEMENTATION` there, exactly
  once) to decode `<img>` source files into RGBA8 pixel buffers.
- `stb_image_resize2.h` (v2.18) -- image resizer. Used by
  `src/output/tbox_raster.c` (`#define STB_IMAGE_RESIZE_IMPLEMENTATION`
  there, exactly once) to scale a decoded image's RGBA8 pixels to an
  `<img>`'s resolved CSS destination size before compositing it.

See ARCHITECTURE.md's image-support section. See LICENSE for both files'
(identical) dual MIT/public-domain license.
