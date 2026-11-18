// A scope that runs the engine under a locale whose decimal point is ',' (the host maintenance, 2026-11-18): a host
// such as a Qt or GTK program sets its locale from the environment at startup, and printf's %g then writes "0,5".
#pragma once

#include "containers/array.h"
#include "cstr/cstr.h"
#include "cstr/format.h"
#include "cstr/mem_ops.h"
#include "os/locale.h"

namespace engine_placeholder::test
{

/// Switches the whole locale (`LC_ALL`, as a host does) to the first installed one of a list of decimal-comma locales
/// for its lifetime, and restores the previous locale when it ends. `Active()` is false when none is installed; the
/// test then skips. The locale is process-wide: only a test that runs alone in its process may use this (ctest runs
/// each Catch2 case in a process of its own).
class CommaLocale
{
public:
    CommaLocale()
    {
        const char* now = os::SetLocale(os::LocaleCategory::All, nullptr);
        const usize length = now == nullptr ? 0 : CStrLength(now);
        if (now == nullptr || length >= previous_.Size())
        {
            return;  // nothing to restore to safely: do not change it
        }
        MemCopy(previous_.Data(), now, length + 1);
        saved_ = true;
        // POSIX names first (nb_NO.utf8 is installed on the the host machine), then the Windows forms.
        const Array<const char*, 12> names = {"nb_NO.utf8",  "nb_NO.UTF-8", "de_DE.UTF-8", "de_DE.utf8",
                                              "fr_FR.UTF-8", "fr_FR.utf8",  "nb-NO",       "de-DE",
                                              "fr-FR",       "de_DE",       "nb_NO",       "fr_FR"};
        for (usize i = 0; i < names.Size(); ++i)
        {
            const char* name = names[i];
            if (os::SetLocale(os::LocaleCategory::All, name) != nullptr && DecimalIsComma())
            {
                name_ = name;
                return;
            }
        }
        Restore();
    }

    ~CommaLocale() { Restore(); }

    CommaLocale(const CommaLocale&) = delete;
    CommaLocale& operator=(const CommaLocale&) = delete;

    /// True when a decimal-comma locale is in effect.
    bool Active() const { return name_ != nullptr; }

    /// The name of the locale in effect, or null.
    const char* Name() const { return name_; }

    /// Whether printf's %f writes a ',' now: the condition the engine's stored text must not depend on.
    static bool DecimalIsComma()
    {
        char text[16];
        FormatTo(text, sizeof(text), "%.1f", 0.5);
        return text[0] == '0' && text[1] == ',';
    }

private:
    void Restore()
    {
        if (saved_)
        {
            os::SetLocale(os::LocaleCategory::All, previous_.Data());
            saved_ = false;
        }
        name_ = nullptr;
    }

    Array<char, 512> previous_ = {};
    bool saved_ = false;
    const char* name_ = nullptr;
};

}  // namespace engine_placeholder::test
