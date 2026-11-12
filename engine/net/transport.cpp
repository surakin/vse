#include "net/transport.h"

namespace engine_placeholder::net
{

const char* ReliabilityName(Reliability reliability)
{
    switch (reliability)
    {
    case Reliability::Unreliable: return "Unreliable";
    case Reliability::UnreliableSequenced: return "UnreliableSequenced";
    case Reliability::ReliableOrdered: return "ReliableOrdered";
    case Reliability::ReliableUnordered: return "ReliableUnordered";
    }
    return "Unknown";
}

const char* DisconnectReasonName(DisconnectReason reason)
{
    switch (reason)
    {
    case DisconnectReason::None: return "None";
    case DisconnectReason::LocalClose: return "LocalClose";
    case DisconnectReason::PeerClosed: return "PeerClosed";
    case DisconnectReason::TimedOut: return "TimedOut";
    case DisconnectReason::Refused: return "Refused";
    case DisconnectReason::Reset: return "Reset";
    case DisconnectReason::ProtocolError: return "ProtocolError";
    }
    return "Unknown";
}

}  // namespace engine_placeholder::net
