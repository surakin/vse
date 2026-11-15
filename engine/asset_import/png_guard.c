/* libpng reports an error by longjmp to the jmp_buf of the read (PngFail in image_decode.cpp). The one setjmp the
 * reader needs is here, in C: MSVC refuses setjmp in a C++ function (C4611, "interaction between '_setjmp' and C++
 * object destruction is non-portable"), found on the Windows PC on 2026-11-15. The body runs below this frame;
 * image_decode.cpp keeps the body and the callbacks free of objects with destructors, so a jump back skips none. Only
 * image_decode.cpp calls it (asset_boundary_check lets this file and that one include png.h). */
#include "png.h"

/* Runs body(context) with the read's error jump set. Returns 1 when the body returned, 0 when libpng jumped back with
 * an error (the read is then unusable and must be destroyed by the caller). */
int engine_asset_png_guard(png_structp png, void (*body)(void*), void* context)
{
    if (setjmp(png_jmpbuf(png)))
    {
        return 0;
    }
    body(context);
    return 1;
}
