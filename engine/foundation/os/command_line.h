#pragma once

#include "containers/array.h"
#include "log/assert.h"
#include "string/string.h"
#include "types.h"

// Portable command-line wrapper (Phase 1 Day 13 — see
// docs/superpowers/specs/2026-08-16-command-line-wrapper-design.md).
// Engine code never touches raw argc/argv directly.
//
// Parsing (this file) is identical on every platform and lives entirely
// in this header since CommandLine is Allocator-templated, like
// Array<T, Allocator>/String<Allocator>. Sourcing the initial argument
// list is genuinely platform-specific (GUI-subsystem Windows gets one
// command string, Android gets launch args from an Intent, not argv at
// all) — StashNativeCommandLine/Detail_NativeCommandLineArgs are the
// only pieces implemented per platform (os/linux/command_line.cpp is
// the only implementation so far, PLAN.md assumption #11), matching
// os.h's own interface-header/per-platform-impl split. Every other
// platform's entry point calls StashNativeCommandLine the same way
// Linux's main.cpp does; it's a no-op wherever FromNative() sources
// args another way instead.
namespace engine_placeholder::os {

void StashNativeCommandLine(int argc, char** argv);

// Not for direct use — called by CommandLine<Allocator>::FromNative().
// Declared here (not file-local to a .cpp) because FromNative() is a
// template and therefore must be defined in this header.
void Detail_NativeCommandLineArgs(int* out_argc, char*** out_argv);

template <typename Allocator>
class CommandLine {
public:
    // Portable parser: any platform whose entry point already has an
    // argc/argv-shaped list can call this directly. Also usable by
    // tests to construct a CommandLine from synthetic argv with no
    // real process launch involved.
    CommandLine(Allocator& allocator, int argc, char** argv)
        : allocator_(&allocator), program_name_(allocator), args_(allocator), flags_(allocator) {
        ENGINE_ASSERT(argc >= 1, "CommandLine: argc must include argv[0]");
        program_name_.Append(StringView(argv[0]));
        for (int i = 1; i < argc; ++i) {
            StringView token(argv[i]);
            if (token.Length() > 2 && token[0] == '-' && token[1] == '-') {
                StringView rest = token.Substring(2, token.Length() - 2);
                usize eq = rest.IndexOf('=');
                if (eq == rest.Length()) {
                    PushFlag(rest, false, StringView());
                } else {
                    PushFlag(rest.Substring(0, eq), true, rest.Substring(eq + 1, rest.Length() - eq - 1));
                }
            } else {
                args_.Push(String<Allocator>(allocator, token));
            }
        }
    }

    // Per-platform sourcing (implemented once per platform — see file
    // comment above).
    static CommandLine FromNative(Allocator& allocator) {
        int argc;
        char** argv;
        Detail_NativeCommandLineArgs(&argc, &argv);
        return CommandLine(allocator, argc, argv);
    }

    CommandLine(const CommandLine&) = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine(CommandLine&&) noexcept = default;
    CommandLine& operator=(CommandLine&&) noexcept = default;

    StringView ProgramName() const { return program_name_.View(); }

    usize ArgCount() const { return args_.Size(); }

    StringView Arg(usize index) const {
        ENGINE_ASSERT(index < args_.Size(), "CommandLine::Arg: index out of range");
        return args_[index].View();
    }

    // True if `--name` (with or without a `=value`) was present.
    bool HasFlag(StringView name) const { return FindFlag(name) != nullptr; }

    // True and sets out_value if `--name=value` was present. False (and
    // out_value untouched) if `name` is absent or was a bare boolean
    // flag with no `=`.
    bool TryGetOption(StringView name, StringView& out_value) const {
        const FlagEntry* entry = FindFlag(name);
        if (entry == nullptr || !entry->has_value) {
            return false;
        }
        out_value = entry->value.View();
        return true;
    }

    // Enumeration by index, for callers that need to walk every
    // flag/option passed without knowing names ahead of time (e.g.
    // Day 13's startup log).
    usize FlagCount() const { return flags_.Size(); }

    StringView FlagName(usize index) const {
        ENGINE_ASSERT(index < flags_.Size(), "CommandLine::FlagName: index out of range");
        return flags_[index].name.View();
    }

    bool FlagHasValue(usize index) const {
        ENGINE_ASSERT(index < flags_.Size(), "CommandLine::FlagHasValue: index out of range");
        return flags_[index].has_value;
    }

    StringView FlagValue(usize index) const {
        ENGINE_ASSERT(index < flags_.Size(), "CommandLine::FlagValue: index out of range");
        ENGINE_ASSERT(flags_[index].has_value, "CommandLine::FlagValue: flag has no value");
        return flags_[index].value.View();
    }

private:
    struct FlagEntry {
        String<Allocator> name;
        bool has_value;
        String<Allocator> value;
    };

    void PushFlag(StringView name, bool has_value, StringView value) {
        // Duplicate flags: last occurrence wins — overwrite in place
        // rather than appending, so lookups don't need to scan for the
        // last match.
        for (usize i = 0; i < flags_.Size(); ++i) {
            if (flags_[i].name.View() == name) {
                flags_[i].has_value = has_value;
                flags_[i].value = String<Allocator>(*allocator_, value);
                return;
            }
        }
        flags_.Push(FlagEntry{String<Allocator>(*allocator_, name), has_value, String<Allocator>(*allocator_, value)});
    }

    const FlagEntry* FindFlag(StringView name) const {
        for (usize i = 0; i < flags_.Size(); ++i) {
            if (flags_[i].name.View() == name) {
                return &flags_[i];
            }
        }
        return nullptr;
    }

    Allocator* allocator_;
    String<Allocator> program_name_;
    Array<String<Allocator>, Allocator> args_;
    Array<FlagEntry, Allocator> flags_;
};

} // namespace engine_placeholder::os
