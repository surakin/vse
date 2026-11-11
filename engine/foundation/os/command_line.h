#pragma once

#include "containers/vector.h"
#include "log/assert.h"
#include "string/string.h"
#include "types.h"

//! Portable command-line parsing: positional arguments plus `--name` and `--name=value` flags.
//! Engine code never touches raw `argc`/`argv` directly.
namespace engine_placeholder::os
{

/// Records the process `argc`/`argv` so [[os::CommandLine::FromNative]] can read them later.
///
/// Call once from the program entry point before `FromNative`. On platforms that source arguments another
/// way this is a no-op. On Windows `argv` holds UTF-8, because every executable's manifest sets the process code page
/// to UTF-8 and the C runtime builds `argv` with it.
/// - Param argc: argument count passed to `main`.
/// - Param argv: argument vector passed to `main`; stored by pointer, not copied.
/// - Thread-safety: not-thread-safe. Call before any other thread reads the command line.
/// - Ownership: `argv` stays owned by the caller and must outlive every later `FromNative` call.
void StashNativeCommandLine(int argc, char** argv);

/// Returns the stashed native `argc`/`argv` for [[os::CommandLine::FromNative]].
/// - Param out_argc: receives the stashed argument count.
/// - Param out_argv: receives the stashed argument vector (borrowed).
/// - Aborts: `StashNativeCommandLine` was never called (Linux backend).
/// - Internal: visible only because `FromNative` is a template defined in this header.
void Detail_NativeCommandLineArgs(int* out_argc, char*** out_argv);

/// Parsed command line: program name, positional arguments and `--name[=value]` flags.
///
/// Tokens starting with `--` and longer than two characters are flags; a flag with `=` carries a value.
/// All other tokens are positional arguments. Repeating a flag keeps the last occurrence. All strings are
/// copied, so the original `argv` need not outlive the object.
///
/// - Thread-safety: thread-compatible. Concurrent const access is safe; construction is not synchronized.
/// - Ownership: owns copies of all strings, allocated from the allocator passed at construction, which must
///   outlive this object.
template <typename Allocator> class CommandLine
{
public:
    /// Parses an `argc`/`argv` pair into a command line.
    ///
    /// Usable on any platform whose entry point already has an `argv`-shaped list, and in tests with
    /// synthetic arguments.
    /// - Param allocator: allocator for the stored strings; must outlive the object.
    /// - Param argc: argument count, including the program name.
    /// - Param argv: argument vector; `argv[0]` is the program name. Borrowed for the call.
    /// - Aborts: `argc` is less than 1.
    CommandLine(Allocator& allocator, int argc, char** argv)
        : allocator_(&allocator), program_name_(allocator), args_(allocator), flags_(allocator)
    {
        ENGINE_ASSERT(argc >= 1, "CommandLine: argc must include argv[0]");
        program_name_.Append(StringView(argv[0]));
        for (int i = 1; i < argc; ++i)
        {
            StringView token(argv[i]);
            if (token.Length() > 2 && token[0] == '-' && token[1] == '-')
            {
                StringView rest = token.Substring(2, token.Length() - 2);
                usize eq = rest.IndexOf('=');
                if (eq == rest.Length())
                {
                    PushFlag(rest, false, StringView());
                }
                else
                {
                    PushFlag(rest.Substring(0, eq), true, rest.Substring(eq + 1, rest.Length() - eq - 1));
                }
            }
            else
            {
                args_.Push(String<Allocator>(allocator, token));
            }
        }
    }

    /// Builds a command line from the platform's native argument source.
    ///
    /// On Linux this is the `argc`/`argv` recorded by [[os::StashNativeCommandLine]].
    /// - Param allocator: allocator for the stored strings; must outlive the object.
    /// - Aborts: no native command line was stashed (Linux backend).
    static CommandLine FromNative(Allocator& allocator)
    {
        int argc;
        char** argv;
        Detail_NativeCommandLineArgs(&argc, &argv);
        return CommandLine(allocator, argc, argv);
    }

    CommandLine(const CommandLine&) = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine(CommandLine&&) noexcept = default;
    CommandLine& operator=(CommandLine&&) noexcept = default;

    /// Returns `argv[0]`.
    /// - Ownership: the view is borrowed from this object and valid for its lifetime.
    StringView ProgramName() const { return program_name_.View(); }

    /// Returns the number of positional arguments, excluding the program name and flags.
    usize ArgCount() const { return args_.Size(); }

    /// Returns the positional argument at `index`, in command-line order.
    /// - Param index: zero-based index below `ArgCount()`.
    /// - Aborts: `index` is out of range.
    /// - Ownership: the view is borrowed from this object and valid for its lifetime.
    StringView Arg(usize index) const
    {
        ENGINE_ASSERT(index < args_.Size(), "CommandLine::Arg: index out of range");
        return args_[index].View();
    }

    /// Returns true if `--name` was present, with or without a `=value`.
    /// - Param name: flag name without the leading `--`.
    bool HasFlag(StringView name) const { return FindFlag(name) != nullptr; }

    /// Looks up the value of `--name=value`.
    /// - Param name: flag name without the leading `--`.
    /// - Param out_value: set to the value on success; untouched on failure.
    /// - Returns: true if the flag was present with a value.
    /// - Fails: returns false if `name` is absent or was a bare flag without `=`.
    /// - Ownership: the value view is borrowed from this object and valid for its lifetime.
    bool TryGetOption(StringView name, StringView& out_value) const
    {
        const FlagEntry* entry = FindFlag(name);
        if (entry == nullptr || !entry->has_value)
        {
            return false;
        }
        out_value = entry->value.View();
        return true;
    }

    /// Returns the number of distinct flags, for callers that walk every flag without knowing names.
    usize FlagCount() const { return flags_.Size(); }

    /// Returns the name of the flag at `index`, without the leading `--`.
    /// - Param index: zero-based index below `FlagCount()`.
    /// - Aborts: `index` is out of range.
    /// - Ownership: the view is borrowed from this object and valid for its lifetime.
    StringView FlagName(usize index) const
    {
        ENGINE_ASSERT(index < flags_.Size(), "CommandLine::FlagName: index out of range");
        return flags_[index].name.View();
    }

    /// Returns whether the flag at `index` was given as `--name=value`.
    /// - Param index: zero-based index below `FlagCount()`.
    /// - Aborts: `index` is out of range.
    bool FlagHasValue(usize index) const
    {
        ENGINE_ASSERT(index < flags_.Size(), "CommandLine::FlagHasValue: index out of range");
        return flags_[index].has_value;
    }

    /// Returns the value of the flag at `index`.
    /// - Param index: zero-based index below `FlagCount()`.
    /// - Aborts: `index` is out of range, or the flag has no value.
    /// - Ownership: the view is borrowed from this object and valid for its lifetime.
    StringView FlagValue(usize index) const
    {
        ENGINE_ASSERT(index < flags_.Size(), "CommandLine::FlagValue: index out of range");
        ENGINE_ASSERT(flags_[index].has_value, "CommandLine::FlagValue: flag has no value");
        return flags_[index].value.View();
    }

private:
    struct FlagEntry
    {
        String<Allocator> name;
        bool has_value;
        String<Allocator> value;
    };

    void PushFlag(StringView name, bool has_value, StringView value)
    {
        // Duplicate flags: last occurrence wins — overwrite in place
        // rather than appending, so lookups don't need to scan for the
        // last match.
        for (usize i = 0; i < flags_.Size(); ++i)
        {
            if (flags_[i].name.View() == name)
            {
                flags_[i].has_value = has_value;
                flags_[i].value = String<Allocator>(*allocator_, value);
                return;
            }
        }
        flags_.Push(FlagEntry{String<Allocator>(*allocator_, name), has_value, String<Allocator>(*allocator_, value)});
    }

    const FlagEntry* FindFlag(StringView name) const
    {
        for (usize i = 0; i < flags_.Size(); ++i)
        {
            if (flags_[i].name.View() == name)
            {
                return &flags_[i];
            }
        }
        return nullptr;
    }

    Allocator* allocator_;
    String<Allocator> program_name_;
    Vector<String<Allocator>, Allocator> args_;
    Vector<FlagEntry, Allocator> flags_;
};

}  // namespace engine_placeholder::os
