#include "net/status.h"

namespace engine_placeholder::net
{

const char* ErrorCodeName(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::Ok: return "ok";
    case ErrorCode::WouldBlock: return "would_block";
    case ErrorCode::Refused: return "refused";
    case ErrorCode::Reset: return "reset";
    case ErrorCode::TimedOut: return "timed_out";
    case ErrorCode::Unreachable: return "unreachable";
    case ErrorCode::AddressInUse: return "address_in_use";
    case ErrorCode::AddressNotAvailable: return "address_not_available";
    case ErrorCode::NotConnected: return "not_connected";
    case ErrorCode::AlreadyConnected: return "already_connected";
    case ErrorCode::MessageTooLarge: return "message_too_large";
    case ErrorCode::Closed: return "closed";
    case ErrorCode::Cancelled: return "cancelled";
    case ErrorCode::Invalid: return "invalid";
    case ErrorCode::LimitExceeded: return "limit_exceeded";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::Other: return "other";
    }
    return "other";
}

}  // namespace engine_placeholder::net
