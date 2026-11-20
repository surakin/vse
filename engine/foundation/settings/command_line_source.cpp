#include "settings/env_source.h"

#include "cstr/format.h"

namespace vse::settings
{

namespace
{

using Text = String<HeapAllocator>;

Text Make(StringView v)
{
    return Text(HeapAllocator::Default(), v);
}

Text Join(StringView a, StringView b, StringView c = StringView(), StringView d = StringView())
{
    Text t = Make(a);
    t.Append(b);
    t.Append(c);
    t.Append(d);
    return t;
}

}  // namespace

CommandLineSource::CommandLineSource(const Registry& registry, int argc, const char* const* argv)
{
    auto fail = [&](Text message) { result_.errors.Push(static_cast<Text&&>(message)); };
    for (int i = 1; i < argc; ++i)
    {
        const StringView arg(argv[i]);
        if (arg == "--")
        {  // the end of the options: this and everything after it belong to the application
            for (; i < argc; ++i)
            {
                result_.rest.Push(Make(StringView(argv[i])));
            }
            break;
        }
        if (arg.Length() < 2 || arg[0] != '-' || arg[1] != '-' || arg.Length() == 2)
        {
            if (arg == "-h")
            {
                result_.help = true;
            }
            else
            {
                result_.rest.Push(Make(arg));
            }
            continue;
        }
        const StringView body = arg.Substring(2);
        const usize eq = body.IndexOf('=');
        const StringView name = body.Substring(0, eq);
        const bool has_inline = eq != body.Length();
        const StringView inline_value = has_inline ? body.Substring(eq + 1) : StringView();
        // The value of an option that takes one: after '=' or the next argument.
        auto take_value = [&](StringView* out)
        {
            if (has_inline)
            {
                *out = inline_value;
                return true;
            }
            if (i + 1 < argc)
            {
                *out = StringView(argv[++i]);
                return true;
            }
            return false;
        };
        if (name == "set")
        {
            StringView pair;
            if (!take_value(&pair))
            {
                fail(Make("--set needs key=value"));
                continue;
            }
            const usize split = pair.IndexOf('=');
            if (split == pair.Length() || split == 0)
            {
                fail(Join("--set ", pair, ": expected key=value"));
                continue;
            }
            const StringView key_text = pair.Substring(0, split);
            const SettingId id = registry.Find(key_text);
            if (!id.IsValid())
            {
                Text message = Join("--set ", key_text, ": no such setting");
                Key suggestion;
                if (SuggestKey(registry, key_text, &suggestion))
                {
                    message.Append(Join("; did you mean '", suggestion.View(), "'?").View());
                }
                fail(static_cast<Text&&>(message));
                continue;
            }
            Put(registry.KeyOf(id), pair.Substring(split + 1), Join("--set ", pair).View());
        }
        else if (name == "settings")
        {
            StringView file;
            if (!take_value(&file) || file.Length() == 0)
            {
                fail(Make("--settings needs a file"));
            }
            else
            {
                result_.settings_files.Push(Make(file));
            }
        }
        else if (name == "list-settings" && !has_inline)
        {
            result_.list_settings = true;
        }
        else if (name == "help" && !has_inline)
        {
            result_.help = true;
        }
        else if (name == "save-settings")
        {
            result_.save_settings = true;
            if (has_inline)
            {
                result_.save_path = Make(
                    inline_value);  // the file form is --save-settings=FILE: a following argument is the application's
            }
        }
        else
        {
            // A declared flag alias: `--gpu NAME`, `--flag` / `--no-flag` for a bool.
            bool matched = false;
            for (u32 k = 0; k < registry.Count() && !matched; ++k)
            {
                const SettingId id = registry.SortedAt(k);
                const SettingDesc& desc = *registry.Desc(id);
                if (desc.flag == nullptr)
                {
                    continue;
                }
                const StringView flag(desc.flag);
                if (desc.type == Type::Bool)
                {
                    if (name == flag)
                    {
                        Put(registry.KeyOf(id), has_inline ? inline_value : StringView("true"),
                            Join("--", name).View());
                        matched = true;
                    }
                    else if (name.StartsWith("no-") && name.Substring(3) == flag)
                    {
                        if (has_inline)
                        {
                            fail(Join("--", name, " takes no value"));
                        }
                        else
                        {
                            Put(registry.KeyOf(id), "false", Join("--", name).View());
                        }
                        matched = true;
                    }
                }
                else if (name == flag)
                {
                    StringView value;
                    if (!take_value(&value))
                    {
                        fail(Join("--", name, " needs a value"));
                    }
                    else
                    {
                        Put(registry.KeyOf(id), value, Join("--", name).View());
                    }
                    matched = true;
                }
            }
            if (!matched)
            {
                result_.rest.Push(Make(arg));
            }
        }
    }
}

void CommandLineSource::Describe(char* out, usize capacity) const
{
    if (capacity != 0)
    {
        FormatTo(out, capacity, "command line");
    }
}

bool CommandLineSource::AddFromProgram(const Key& key, StringView text, StringView origin)
{
    StoredValue existing;
    if (Get(key.View(), &existing).Ok())
    {
        return false;
    }
    Put(key, text, origin);
    return true;
}

}  // namespace vse::settings
