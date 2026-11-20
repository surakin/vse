#include "debug/crash_reason.h"

namespace vse::debug
{

const char* CrashReasonName(CrashReason reason)
{
    switch (reason)
    {
    case CrashReason::None: return "None";
    case CrashReason::AccessViolation: return "AccessViolation";
    case CrashReason::BusError: return "BusError";
    case CrashReason::IllegalInstruction: return "IllegalInstruction";
    case CrashReason::ArithmeticFault: return "ArithmeticFault";
    case CrashReason::StackOverflow: return "StackOverflow";
    case CrashReason::Abort: return "Abort";
    case CrashReason::AssertFailed: return "AssertFailed";
    case CrashReason::EnsureFailed: return "EnsureFailed";
    }
    return "Unknown";
}

}  // namespace vse::debug
