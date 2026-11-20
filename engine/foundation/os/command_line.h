#pragma once

#include "containers/span.h"
#include "containers/vector.h"
#include "log/assert.h"
#include "string/string.h"
#include "types.h"

//! Portable command-line parsing: positional arguments plus `--name` and `--name=value` flags, and, for a program that
//! declares its options, `--name value` as well, with unknown options and missing values reported. Engine, game and
//! tool code never touches raw `argc`/`argv` directly (`cmdline_access_check`).
namespace vse::os
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

/// One option a program accepts, declared to [[os::CommandLine]] so that it can tell `--name value` from `--name`
/// followed by a positional argument.
///
/// - Thread-safety: immutable value.
/// - Ownership: `name` is borrowed (a string literal).
struct CommandLineOption
{
    /// The name without the leading `--`; a name that starts with `-` (`-I`) is written with one dash.
    const char* name;
    /// Whether the option has a value (`--name value` or `--name=value`); if not, `--name=value` is an error.
    bool takes_value;
    /// Whether the option may be given more than once and every occurrence kept (`--link a --link b`).
    bool repeatable = false;
};

/// Parsed command line: program name, positional arguments and `--name[=value]` flags.
///
/// Without declared options (the constructor with no option list), tokens starting with `--` and longer than two
/// characters are flags; a flag with `=` carries a value, and all other tokens are positional arguments. With declared
/// options, a flag that takes a value also takes the next token (`--root DIR`), a flag that is not declared, a missing
/// value or a value given to a flag that takes none is an error ([[os::CommandLine::Ok]], [[os::CommandLine::Error]]),
/// a bare `--` ends the flags (everything after it is positional), and a token with one dash is positional unless a
/// one-dash option of that name is declared (`-1`, `-`). Repeating a flag keeps the last occurrence. All strings are
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
        : allocator_(&allocator), program_name_(allocator), args_(allocator), flags_(allocator), error_(allocator)
    {
        Parse(argc, argv, nullptr, 0, false);
    }

    /// Parses an `argc`/`argv` pair against the options the program declares.
    ///
    /// - Param allocator: allocator for the stored strings; must outlive the object.
    /// - Param argc: argument count, including the program name.
    /// - Param argv: argument vector; `argv[0]` is the program name. Borrowed for the call.
    /// - Param options: the options the program accepts; borrowed for the call (the names are not copied).
    /// - Aborts: `argc` is less than 1.
    /// - Fails: a problem with the arguments is not an abort: [[os::CommandLine::Ok]] is false and
    ///   [[os::CommandLine::Error]] says which (the first one found); the flags before it are kept.
    CommandLine(Allocator& allocator, int argc, char** argv, Span<const CommandLineOption> options)
        : allocator_(&allocator), program_name_(allocator), args_(allocator), flags_(allocator), error_(allocator)
    {
        Parse(argc, argv, options.Data(), options.Size(), true);
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

    /// True when the command line followed the declared options (always true without declared options).
    bool Ok() const { return error_.Length() == 0; }

    /// A sentence naming the first problem (an unknown option, a missing value), or empty when [[os::CommandLine::Ok]].
    /// - Ownership: the view is borrowed from this object and valid for its lifetime.
    StringView Error() const { return error_.View(); }

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

    /// The value of `--name`, or `fallback` when it was not given with a value: a NUL-terminated string for the call
    /// that needs a C string (paths, numbers).
    /// - Param name: flag name without the leading `--`.
    /// - Param fallback: returned (not copied) when there is no such option.
    /// - Ownership: the result is borrowed from this object (or is `fallback`) and valid for its lifetime.
    const char* OptionOr(StringView name, const char* fallback) const
    {
        const FlagEntry* entry = FindFlag(name);
        return entry != nullptr && entry->has_value ? entry->value.CStr() : fallback;
    }

    /// Returns how many times the option was given (more than one only for a repeatable option).
    /// - Param name: option name without the leading `--` (`-I` for a one-dash option).
    usize OptionCount(StringView name) const
    {
        usize count = 0;
        for (usize i = 0; i < flags_.Size(); ++i)
        {
            if (flags_[i].name.View() == name && flags_[i].has_value)
            {
                count += 1;
            }
        }
        return count;
    }

    /// Returns the value of the `index`-th occurrence of an option, in command-line order.
    /// - Param name: option name as for `OptionCount`.
    /// - Param index: zero-based index below `OptionCount(name)`.
    /// - Aborts: `index` is out of range.
    /// - Ownership: the view is borrowed from this object and valid for its lifetime.
    StringView OptionAt(StringView name, usize index) const
    {
        for (usize i = 0; i < flags_.Size(); ++i)
        {
            if (flags_[i].name.View() == name && flags_[i].has_value)
            {
                if (index == 0)
                {
                    return flags_[i].value.View();
                }
                index -= 1;
            }
        }
        ENGINE_ASSERT(false, "CommandLine::OptionAt: index out of range");
        return StringView();
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

    static const CommandLineOption* FindOption(StringView name, const CommandLineOption* options, usize count)
    {
        for (usize k = 0; k < count; ++k)
        {
            if (name == StringView(options[k].name))
            {
                return &options[k];
            }
        }
        return nullptr;
    }

    void Parse(int argc, char** argv, const CommandLineOption* options, usize option_count, bool declared)
    {
        ENGINE_ASSERT(argc >= 1, "CommandLine: argc must include argv[0]");
        program_name_.Append(StringView(argv[0]));
        bool flags_ended = false;
        for (int i = 1; i < argc; ++i)
        {
            const StringView token(argv[i]);
            const bool is_long = !flags_ended && token.Length() > 2 && token[0] == '-' && token[1] == '-';
            const bool is_short = declared && !flags_ended && token.Length() >= 2 && token[0] == '-' &&
                                  token[1] != '-' &&
                                  FindOption(token.Substring(0, 2), options, option_count) != nullptr;
            if (declared && !flags_ended && token == StringView("--"))
            {
                flags_ended = true;
            }
            else if (is_long && !declared)
            {
                const StringView rest = token.Substring(2, token.Length() - 2);
                const usize eq = rest.IndexOf('=');
                if (eq == rest.Length())
                {
                    PushFlag(rest, false, StringView(), false);
                }
                else
                {
                    PushFlag(rest.Substring(0, eq), true, rest.Substring(eq + 1, rest.Length() - eq - 1), false);
                }
            }
            else if (is_long)
            {
                const StringView rest = token.Substring(2, token.Length() - 2);
                const usize eq = rest.IndexOf('=');
                const bool has_eq = eq != rest.Length();
                const StringView name = has_eq ? rest.Substring(0, eq) : rest;
                const CommandLineOption* option = FindOption(name, options, option_count);
                if (option == nullptr)
                {
                    SetError("unknown option --", name, "");
                }
                else if (!option->takes_value)
                {
                    if (has_eq)
                    {
                        SetError("--", name, " takes no value");
                    }
                    PushFlag(name, false, StringView(), option->repeatable);
                }
                else if (has_eq)
                {
                    PushFlag(name, true, rest.Substring(eq + 1, rest.Length() - eq - 1), option->repeatable);
                }
                else if (i + 1 < argc)
                {
                    PushFlag(name, true, StringView(argv[++i]), option->repeatable);
                }
                else
                {
                    SetError("--", name, " needs a value");
                }
            }
            else if (is_short)
            {
                // A one-dash option (`-I dir`, `-Idir`): its declared name is the first two characters of the token.
                const StringView name = token.Substring(0, 2);
                const CommandLineOption* option = FindOption(name, options, option_count);
                if (!option->takes_value)
                {
                    PushFlag(name, false, StringView(), option->repeatable);
                }
                else if (token.Length() > 2)
                {
                    PushFlag(name, true, token.Substring(2), option->repeatable);
                }
                else if (i + 1 < argc)
                {
                    PushFlag(name, true, StringView(argv[++i]), option->repeatable);
                }
                else
                {
                    SetError("", name, " needs a value");
                }
            }
            else
            {
                args_.Push(String<Allocator>(*allocator_, token));
            }
        }
    }

    void SetError(const char* before, StringView name, const char* after)
    {
        if (error_.Length() == 0)
        {
            error_.Append(StringView(before));
            error_.Append(name);
            error_.Append(StringView(after));
        }
    }

    void PushFlag(StringView name, bool has_value, StringView value, bool repeatable)
    {
        // Duplicate flags: last occurrence wins — overwrite in place
        // rather than appending, so lookups don't need to scan for the
        // last match.
        for (usize i = 0; !repeatable && i < flags_.Size(); ++i)
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
    String<Allocator> error_;
};

}  // namespace vse::os
