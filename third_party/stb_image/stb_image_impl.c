/* The JPEG decoder of stb_image (v2.32, public domain / MIT), compiled alone: baseline and progressive 8-bit JPEG, no PNG, no other format, no
   stdio, no SIMD (the integer scalar paths give the same pixels on every machine), no float or HDR code. Only engine/asset_import/image_decode.cpp
   includes stb_image.h. See VENDORED.md for the definitions and the reasons. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 16384
/* The library's asserts guard its own invariants; a hostile image must never abort, so they compile to nothing. */
#define STBI_ASSERT(x) ((void)0)
#include "stb_image.h"
