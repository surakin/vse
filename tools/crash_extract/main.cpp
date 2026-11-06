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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "io/file.h"

namespace {

bool ReadWholeFile(const char* path, engine_placeholder::Vector<engine_placeholder::u8>* bytes) {
    namespace io = engine_placeholder::io;
    constexpr std::uint64_t kMaxEnvelopeBytes = 256u * 1024u * 1024u;
    io::Path file;
    return io::Path::Parse(engine_placeholder::StringView(path), io::NativePathStyle(), &file) && io::ReadFile(file, bytes, kMaxEnvelopeBytes).Ok();
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

    engine_placeholder::Vector<engine_placeholder::u8> envelope;
    if (!ReadWholeFile(envelope_path, &envelope)) {
        std::fprintf(stderr, "crash_extract: cannot read %s\n", envelope_path);
        return 2;
    }
    const char* data = reinterpret_cast<const char*>(envelope.Data());
    const std::size_t size = envelope.Size();
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
        // Written atomically (a temporary file, then a rename), so the output either is complete or does not exist.
        namespace io = engine_placeholder::io;
        io::Path output;
        const bool written = io::Path::Parse(engine_placeholder::StringView(output_path), io::NativePathStyle(), &output) &&
                             io::WriteFileAtomic(output, dump.data, dump.size).Ok();
        if (!written) {
            std::fprintf(stderr, "crash_extract: cannot write %s\n", output_path);
            status = 2;
        } else {
            std::printf("%s\n", output_path);
        }
    }
    return status;
}
