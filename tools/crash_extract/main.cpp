// crash_extract: pulls the minidump out of a crash envelope.
//
//   crash_extract <envelope> [<output.dmp>]
//
// The crash handler (engine/foundation/debug/crash_handler.h) leaves one `.envelope` file per crash in its dump directory;
// the minidump inside it is what `lldb -c`, `minidump-stackwalk` and WinDbg open. Without an output path the dump is
// written next to the envelope with the extension `.dmp`.
//
// Exit status: 0 the dump was written; 1 the envelope has no minidump; 2 the envelope is damaged or a file could not be
// read or written; 64 wrong arguments. A dev tool: never linked into the engine or a game.
#include "envelope.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool ReadWholeFile(const char* path, char** data, std::size_t* size) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    constexpr std::size_t kMaxEnvelopeBytes = 256u * 1024u * 1024u;
    std::size_t capacity = 1 << 16;
    char* buffer = static_cast<char*>(std::malloc(capacity));
    std::size_t used = 0;
    while (buffer != nullptr) {
        if (used == capacity) {
            if (capacity >= kMaxEnvelopeBytes) {
                std::free(buffer);
                std::fclose(file);
                return false;
            }
            capacity *= 2;
            char* bigger = static_cast<char*>(std::realloc(buffer, capacity));
            if (bigger == nullptr) {
                std::free(buffer);
                buffer = nullptr;
                break;
            }
            buffer = bigger;
        }
        const std::size_t n = std::fread(buffer + used, 1, capacity - used, file);
        if (n == 0) {
            break;
        }
        used += n;
    }
    const bool failed = std::ferror(file) != 0 || buffer == nullptr;
    std::fclose(file);
    if (failed) {
        std::free(buffer);
        return false;
    }
    *data = buffer;
    *size = used;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: crash_extract <envelope> [<output.dmp>]\n");
        return 64;
    }
    const char* envelope_path = argv[1];
    char default_output[4096];
    const char* output_path = argc == 3 ? argv[2] : nullptr;
    if (output_path == nullptr) {
        // Only an input named *.envelope gets a derived name; anything else would end up as foo.dmp.dmp or overwrite the input.
        const std::size_t length = std::strlen(envelope_path);
        const char* extension = ".envelope";
        const std::size_t extension_length = std::strlen(extension);
        if (length <= extension_length || std::strcmp(envelope_path + length - extension_length, extension) != 0) {
            std::fprintf(stderr, "crash_extract: %s does not end in .envelope; name the output file\n", envelope_path);
            return 2;
        }
        const std::size_t stem = length - extension_length;
        if (stem + 5 > sizeof(default_output)) {
            std::fprintf(stderr, "crash_extract: the path is too long\n");
            return 2;
        }
        std::memcpy(default_output, envelope_path, stem);
        std::memcpy(default_output + stem, ".dmp", 5);
        output_path = default_output;
    }
    if (std::strcmp(output_path, envelope_path) == 0) {
        std::fprintf(stderr, "crash_extract: the output would overwrite the envelope\n");
        return 2;
    }

    char* data = nullptr;
    std::size_t size = 0;
    if (!ReadWholeFile(envelope_path, &data, &size)) {
        std::fprintf(stderr, "crash_extract: cannot read %s\n", envelope_path);
        return 2;
    }
    crash_extract::Minidump dump;
    const crash_extract::EnvelopeResult result = crash_extract::FindMinidump(data, size, &dump);
    int status = 0;
    if (result == crash_extract::EnvelopeResult::Malformed) {
        std::fprintf(stderr, "crash_extract: %s is not a complete envelope\n", envelope_path);
        status = 2;
    } else if (result == crash_extract::EnvelopeResult::NoMinidump) {
        std::fprintf(stderr, "crash_extract: %s holds no minidump\n", envelope_path);
        status = 1;
    } else {
        // Written to a temporary name and renamed, so the output either is complete or does not exist.
        char temporary[4200];
        const int n = std::snprintf(temporary, sizeof(temporary), "%s.partial", output_path);
        bool written = n > 0 && static_cast<std::size_t>(n) < sizeof(temporary);
        if (written) {
            std::FILE* out = std::fopen(temporary, "wb");
            written = out != nullptr;
            if (out != nullptr) {
                written = std::fwrite(dump.data, 1, dump.size, out) == dump.size;
                written = (std::fclose(out) == 0) && written; // always closed
            }
            written = written && std::rename(temporary, output_path) == 0;
            if (!written) {
                std::remove(temporary);
            }
        }
        if (!written) {
            std::fprintf(stderr, "crash_extract: cannot write %s\n", output_path);
            status = 2;
        } else {
            std::printf("%s\n", output_path);
        }
    }
    std::free(data);
    return status;
}
