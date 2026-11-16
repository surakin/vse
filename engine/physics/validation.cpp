#include "physics/validation.h"

namespace engine_placeholder::physics
{

const char* ValidationName(Validation value)
{
    switch (value)
    {
    case Validation::Ok: return "ok";
    case Validation::NotFinite: return "not_finite";
    case Validation::NotPositive: return "not_positive";
    case Validation::OutOfRange: return "out_of_range";
    case Validation::UnknownKind: return "unknown_kind";
    case Validation::NotNormalized: return "not_normalized";
    }
    return "unknown";
}

}  // namespace engine_placeholder::physics
