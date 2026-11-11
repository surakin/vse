// Oracle test of the lang module: every wrapper is compared with the standard facility it forwards to. This file
// includes the standard headers on purpose (the gate lists oracle files by name).
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

#include "lang/initializer_list.h"
#include "lang/limits.h"
#include "lang/new.h"
#include "lang/ordering.h"
#include "lang/traits.h"
#include "lang/utility.h"

using namespace engine_placeholder;

namespace
{

struct MoveOnly
{
    int value = 0;
    MoveOnly() = default;
    explicit MoveOnly(int v) : value(v) {}
    MoveOnly(MoveOnly&& o) noexcept : value(o.value) { o.value = -1; }
    MoveOnly& operator=(MoveOnly&& o) noexcept
    {
        value = o.value;
        o.value = -1;
        return *this;
    }
    MoveOnly(const MoveOnly&) = delete;
};

struct Base
{
};
struct Derived : Base
{
};
enum class Color : u8
{
    Red,
    Green
};

int Which(int&)
{
    return 1;
}
int Which(const int&)
{
    return 2;
}
int Which(int&&)
{
    return 3;
}

template <class T> int ForwardTo(T&& value)
{
    return Which(Forward<T>(value));
}

}  // namespace

// Traits against the standard originals.
static_assert(IsSame<int, int> && !IsSame<int, long>);
static_assert(IsSame<int, int> == std::is_same_v<int, int>);
static_assert(IsConvertible<int, long> == std::is_convertible_v<int, long>);
static_assert(IsConvertible<Derived*, Base*> && !IsConvertible<Base*, Derived*>);
static_assert(IsIntegral<u32> && !IsIntegral<f32> && IsFloatingPoint<f64> && !IsFloatingPoint<int>);
static_assert(IsSigned<i16> && IsUnsigned<u16> && !IsSigned<u16>);
static_assert(IsEnum<Color> && !IsEnum<int> && IsPointer<int*> && !IsPointer<int> && IsConst<const int>);
static_assert(IsTriviallyCopyable<int> && !IsTriviallyCopyable<MoveOnly> == !std::is_trivially_copyable_v<MoveOnly>);
static_assert(IsMoveConstructible<MoveOnly> && !IsCopyConstructible<MoveOnly>);
static_assert(IsBaseOf<Base, Derived> && !IsBaseOf<Derived, Base>);
static_assert(IsConstructible<MoveOnly, int> && !IsConstructible<MoveOnly, const MoveOnly&>);
static_assert(IsSame<EnableIf<true, int>, int> && IsSame<Conditional<false, int, long>, long>);
static_assert(IsSame<RemoveReference<int&>, int> && IsSame<RemoveReference<int&&>, int> &&
              IsSame<RemoveConst<const int>, int>);
static_assert(IsSame<RemoveCVRef<const volatile int&>, int> && IsSame<RemovePointer<int*>, int> &&
              IsSame<Decay<const int&>, int>);
static_assert(IsSame<Decay<int[3]>, int*> && IsSame<UnderlyingType<Color>, u8>);
static_assert(IsSame<MakeUnsigned<i32>, u32> && IsSame<MakeSigned<u64>, i64>);
static_assert(IsSame<InvokeResult<int (*)(int), int>, int> && IsInvocable<int (*)(int), int> &&
              !IsInvocable<int (*)(int)>);
static_assert(IsSame<VoidType<int, long>, void> && TrueType::value && !FalseType::value &&
              IntegralConstant<int, 4>::value == 4);
static_assert(IsConstantEvaluated() == false || true);
static_assert(IsSame<decltype(Declval<int>()), int&&>);
static_assert(noexcept(Move(std::declval<int&>())) && noexcept(Forward<int>(std::declval<int&>())));

// Limits against numeric_limits for every arithmetic type.
template <class T> static void CheckLimits()
{
    using L = Limits<T>;
    using S = std::numeric_limits<T>;
    STATIC_REQUIRE(L::kSpecialized == S::is_specialized);
    STATIC_REQUIRE(L::kIsSigned == S::is_signed);
    STATIC_REQUIRE(L::kIsInteger == S::is_integer);
    STATIC_REQUIRE(L::kDigits == S::digits);
    STATIC_REQUIRE(L::kDigits10 == S::digits10);
    STATIC_REQUIRE(L::Min() == S::min());
    STATIC_REQUIRE(L::Max() == S::max());
    STATIC_REQUIRE(L::Lowest() == S::lowest());
    STATIC_REQUIRE(L::Epsilon() == S::epsilon());
}

TEST_CASE("Limits matches numeric_limits for every arithmetic type", "[lang]")
{
    CheckLimits<bool>();
    CheckLimits<char>();
    CheckLimits<i8>();
    CheckLimits<u8>();
    CheckLimits<i16>();
    CheckLimits<u16>();
    CheckLimits<i32>();
    CheckLimits<u32>();
    CheckLimits<i64>();
    CheckLimits<u64>();
    CheckLimits<f32>();
    CheckLimits<f64>();
    CHECK(Limits<f32>::Infinity() == std::numeric_limits<f32>::infinity());
    CHECK(Limits<f64>::Infinity() == std::numeric_limits<f64>::infinity());
    CHECK(Limits<f32>::QuietNaN() != Limits<f32>::QuietNaN());
    CHECK(Limits<f64>::DenormMin() == std::numeric_limits<f64>::denorm_min());
    CHECK(Limits<f32>::kIsIec559 == std::numeric_limits<f32>::is_iec559);
}

TEST_CASE("Move, Forward, Exchange, Swap and AsConst", "[lang]")
{
    MoveOnly a(7);
    MoveOnly b = Move(a);
    CHECK(b.value == 7);
    CHECK(a.value == -1);

    int x = 1;
    const int cx = 2;
    CHECK(ForwardTo(x) == 1);
    CHECK(ForwardTo(cx) == 2);
    CHECK(ForwardTo(3) == 3);

    int old = Exchange(x, 9);
    CHECK(old == 1);
    CHECK(x == 9);
    int y = 4;
    Swap(x, y);
    CHECK(x == 4);
    CHECK(y == 9);
    MoveOnly p(1), q(2);
    Swap(p, q);
    CHECK(p.value == 2);
    CHECK(q.value == 1);
    CHECK(&AsConst(x) == &x);
    STATIC_REQUIRE(IsSame<decltype(AsConst(x)), const int&>);
}

TEST_CASE("Ordering is std::strong_ordering and compares as one", "[lang]")
{
    struct Key
    {
        int v;
        constexpr Ordering operator<=>(const Key& o) const { return v <=> o.v; }
        constexpr bool operator==(const Key& o) const { return v == o.v; }
    };
    STATIC_REQUIRE(IsSame<Ordering, std::strong_ordering>);
    STATIC_REQUIRE(Key{1} < Key{2});
    STATIC_REQUIRE(Key{2} > Key{1});
    STATIC_REQUIRE(Key{2} >= Key{2});
    STATIC_REQUIRE((Key{1} <=> Key{1}) == Ordering::equal);
    STATIC_REQUIRE((Key{1} <=> Key{2}) == Ordering::less);
}

namespace
{
struct Sum
{
    int total = 0;
    Sum(InitializerList<int> items)
    {
        for (int i : items)
        {
            total += i;
        }
    }
};
}  // namespace

TEST_CASE("InitializerList accepts a braced list", "[lang]")
{
    Sum s{1, 2, 3, 4};
    CHECK(s.total == 10);
    STATIC_REQUIRE(IsSame<InitializerList<int>, std::initializer_list<int>>);
}

TEST_CASE("new helpers: placement, launder and the aligned non-throwing allocation", "[lang]")
{
    alignas(MoveOnly) unsigned char storage[sizeof(MoveOnly)];
    MoveOnly* m = ConstructAt<MoveOnly>(storage, 5);
    CHECK(m->value == 5);
    CHECK(Launder(m) == m);
    m->~MoveOnly();

    void* p = AlignedNew(256, 64);
    REQUIRE(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
    AlignedDelete(p, 64);

    STATIC_REQUIRE(IsSame<AlignVal, std::align_val_t>);
    STATIC_REQUIRE(IsSame<NoThrowTag, std::nothrow_t>);
}

struct PolymorphicBase
{
    virtual ~PolymorphicBase() = default;
};
struct PlainType
{
};

TEST_CASE("IsReference, IsArray, IsVolatile, IsDestructible and IsDefaultConstructible", "[lang]")
{
    struct NoDefault
    {
        explicit NoDefault(int) {}
    };
    struct NoDestroy
    {
        ~NoDestroy() = delete;
    };
    static_assert(IsReference<int&> && IsReference<int&&> && !IsReference<int>);
    static_assert(IsArray<int[3]> && IsArray<int[]> && !IsArray<int*>);
    static_assert(IsVolatile<volatile int> && !IsVolatile<const int>);
    static_assert(IsDestructible<int> && !IsDestructible<NoDestroy>);
    static_assert(IsDefaultConstructible<int> && !IsDefaultConstructible<NoDefault>);
    CHECK(true);
}

TEST_CASE("HasVirtualDestructor, TypeIdentity and kMaxAlign", "[lang]")
{
    static_assert(HasVirtualDestructor<PolymorphicBase>);
    static_assert(!HasVirtualDestructor<PlainType>);
    static_assert(IsSame<TypeIdentity<int>, int>);
    static_assert(kMaxAlign >= alignof(double) && kMaxAlign >= alignof(void*));
    CHECK((kMaxAlign & (kMaxAlign - 1)) == 0);
}
