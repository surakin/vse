#include "asset_import/image_decode.h"

#include "cstr/mem_ops.h"
#include "lang/status_try.h"

// The decoders are C libraries with their own headers; this is the one file that includes them.
#include "png.h"
#include "stb_image.h"

namespace engine_placeholder::asset
{

namespace
{

constexpr u32 kMaxSide = 16384;

Status Refuse(GltfError* error, const char* what, ErrorCode code = ErrorCode::Invalid)
{
    error->Set(StringView(), StringView(what));
    return Status::Make(code, "an image is refused");
}

// The state of one PNG read. Plain data: the functions that call `setjmp` hold nothing else, so a jump back skips no
// destructor.
struct PngJob
{
    const byte* data = nullptr;
    usize size = 0;
    usize position = 0;
    png_structp png = nullptr;
    png_infop info = nullptr;
    u32 width = 0;
    u32 height = 0;
    int bit_depth = 0;
    int color_type = 0;
    bool has_transparency = false;
    char message[96] = {};
};

void SetMessage(PngJob* job, const char* text)
{
    usize i = 0;
    for (; text[i] != '\0' && i + 1 < sizeof(job->message); ++i)
    {
        job->message[i] = text[i];
    }
    job->message[i] = '\0';
}

void PngReadData(png_structp png, png_bytep out, png_size_t length)
{
    PngJob* job = static_cast<PngJob*>(png_get_io_ptr(png));
    if (length > job->size - job->position)
    {
        png_error(png, "the file ends early");
    }
    MemCopy(out, job->data + job->position, length);
    job->position += length;
}

void PngFail(png_structp png, png_const_charp message)
{
    PngJob* job = static_cast<PngJob*>(png_get_error_ptr(png));
    SetMessage(job, message);
    png_longjmp(png, 1);
}

void PngWarn(png_structp, png_const_charp) {}

// Reads the header. False (with `message`) when it is not a PNG libpng accepts; on true the read is open.
bool PngOpen(PngJob* job)
{
    job->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, job, &PngFail, &PngWarn);
    if (job->png == nullptr)
    {
        SetMessage(job, "the PNG reader could not be created");
        return false;
    }
    job->info = png_create_info_struct(job->png);
    if (job->info == nullptr)
    {
        png_destroy_read_struct(&job->png, nullptr, nullptr);
        SetMessage(job, "the PNG reader could not be created");
        return false;
    }
    if (setjmp(png_jmpbuf(job->png)))
    {
        png_destroy_read_struct(&job->png, &job->info, nullptr);
        return false;
    }
    png_set_user_limits(job->png, kMaxSide, kMaxSide);
    png_set_chunk_malloc_max(job->png, 4u << 20);
    png_set_chunk_cache_max(job->png, 128);
    png_set_read_fn(job->png, job, &PngReadData);
    png_read_info(job->png, job->info);
    png_uint_32 width = 0;
    png_uint_32 height = 0;
    int interlace = 0;
    png_get_IHDR(job->png, job->info, &width, &height, &job->bit_depth, &job->color_type, &interlace, nullptr, nullptr);
    job->width = width;
    job->height = height;
    job->has_transparency = png_get_valid(job->png, job->info, PNG_INFO_tRNS) != 0;
    return true;
}

// Decodes the rows into `out` (`width * height * 4` bytes) and closes the read.
bool PngRead(PngJob* job, byte* out)
{
    if (setjmp(png_jmpbuf(job->png)))
    {
        png_destroy_read_struct(&job->png, &job->info, nullptr);
        return false;
    }
    if (job->color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(job->png);
    }
    if (job->color_type == PNG_COLOR_TYPE_GRAY && job->bit_depth < 8)
    {
        png_set_expand_gray_1_2_4_to_8(job->png);
    }
    if (job->has_transparency)
    {
        png_set_tRNS_to_alpha(job->png);
    }
    if (job->bit_depth == 16)
    {
        png_set_scale_16(job->png);
    }
    if (job->color_type == PNG_COLOR_TYPE_GRAY || job->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        png_set_gray_to_rgb(job->png);
    }
    if ((job->color_type & PNG_COLOR_MASK_ALPHA) == 0 && !job->has_transparency)
    {
        png_set_add_alpha(job->png, 0xFF, PNG_FILLER_AFTER);
    }
    const int passes = png_set_interlace_handling(job->png);
    png_read_update_info(job->png, job->info);
    const usize stride = static_cast<usize>(job->width) * 4;
    if (png_get_rowbytes(job->png, job->info) != stride)
    {
        png_error(job->png, "the decoded rows are not RGBA");
    }
    for (int pass = 0; pass < passes; ++pass)
    {
        for (u32 y = 0; y < job->height; ++y)
        {
            png_read_row(job->png, out + static_cast<usize>(y) * stride, nullptr);
        }
    }
    png_read_end(job->png, nullptr);
    png_destroy_read_struct(&job->png, &job->info, nullptr);
    return true;
}

Status CheckSize(u64 width, u64 height, u64 max_pixels, GltfError* error)
{
    if (width == 0 || height == 0)
    {
        return Refuse(error, "has no pixels");
    }
    if (width > kMaxSide || height > kMaxSide || width * height > max_pixels)
    {
        error->SetNumber(StringView(), StringView("has more pixels than the import allows; its width times height is "),
                         width * height);
        return Status::Make(ErrorCode::TooLarge, "an image is too large");
    }
    return Status::Success();
}

Status DecodePng(ByteSpan bytes, u64 max_pixels, DecodedImage* out, GltfError* error)
{
    PngJob job;
    job.data = bytes.Data();
    job.size = bytes.Size();
    if (!PngOpen(&job))
    {
        error->Set(StringView(), StringView(job.message));
        return Status::Make(ErrorCode::Invalid, "a PNG is refused");
    }
    const Status size_ok = CheckSize(job.width, job.height, max_pixels, error);
    if (size_ok.Failed())
    {
        png_destroy_read_struct(&job.png, &job.info, nullptr);
        return size_ok;
    }
    out->width = job.width;
    out->height = job.height;
    out->has_alpha = (job.color_type & PNG_COLOR_MASK_ALPHA) != 0 || job.has_transparency;
    out->reduced_depth = job.bit_depth == 16;
    if (!out->rgba.TryResize(static_cast<usize>(job.width) * job.height * 4))
    {
        png_destroy_read_struct(&job.png, &job.info, nullptr);
        return Refuse(error, "is too large for memory", ErrorCode::LimitExceeded);
    }
    if (!PngRead(&job, out->rgba.Data()))
    {
        error->Set(StringView(), StringView(job.message));
        return Status::Make(ErrorCode::Invalid, "a PNG is refused");
    }
    return Status::Success();
}

Status DecodeJpeg(ByteSpan bytes, u64 max_pixels, DecodedImage* out, GltfError* error)
{
    int width = 0;
    int height = 0;
    int components = 0;
    const int length = static_cast<int>(bytes.Size());
    if (!stbi_info_from_memory(bytes.Data(), length, &width, &height, &components))
    {
        const char* why = stbi_failure_reason();
        error->Set(StringView(), StringView(why != nullptr ? why : "is not a JPEG the decoder reads"));
        return Status::Make(ErrorCode::Invalid, "a JPEG is refused");
    }
    ENGINE_TRY(CheckSize(static_cast<u64>(width > 0 ? width : 0), static_cast<u64>(height > 0 ? height : 0), max_pixels,
                         error));
    stbi_uc* pixels = stbi_load_from_memory(bytes.Data(), length, &width, &height, &components, 4);
    if (pixels == nullptr)
    {
        const char* why = stbi_failure_reason();
        error->Set(StringView(), StringView(why != nullptr ? why : "is not a JPEG the decoder reads"));
        return Status::Make(ErrorCode::Invalid, "a JPEG is refused");
    }
    const usize count = static_cast<usize>(width) * static_cast<usize>(height) * 4;
    const bool resized = out->rgba.TryResize(count);
    if (resized)
    {
        MemCopy(out->rgba.Data(), pixels, count);
    }
    stbi_image_free(pixels);
    if (!resized)
    {
        return Refuse(error, "is too large for memory", ErrorCode::LimitExceeded);
    }
    out->width = static_cast<u32>(width);
    out->height = static_cast<u32>(height);
    out->has_alpha = false;
    out->reduced_depth = false;
    return Status::Success();
}

}  // namespace

ImageFormat SniffImageFormat(ByteSpan bytes)
{
    static const byte kPng[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.Size() >= 8 && MemCompare(bytes.Data(), kPng, 8) == 0)
    {
        return ImageFormat::Png;
    }
    if (bytes.Size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
    {
        return ImageFormat::Jpeg;
    }
    return ImageFormat::Unknown;
}

Status DecodePixels(ByteSpan bytes, u64 max_pixels, DecodedImage* out, GltfError* error)
{
    *out = DecodedImage();
    if (bytes.Size() > 0x7fffffffu)
    {
        return Refuse(error, "is larger than 2 GiB", ErrorCode::TooLarge);
    }
    switch (SniffImageFormat(bytes))
    {
    case ImageFormat::Png: return DecodePng(bytes, max_pixels, out, error);
    case ImageFormat::Jpeg: return DecodeJpeg(bytes, max_pixels, out, error);
    case ImageFormat::Unknown: break;
    }
    return Refuse(error, "is neither a PNG nor a JPEG");
}

}  // namespace engine_placeholder::asset
