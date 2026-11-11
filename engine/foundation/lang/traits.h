#pragma once

#include <type_traits>

//! Type traits (Phase 16): the engine's spelling of `<type_traits>`. Variable templates are `IsX<T>`, type
//! transformations are aliases `Remove...<T>`; each is exactly the standard facility of the same meaning.
namespace engine_placeholder
{

/// A compile-time constant of type `T` (`std::integral_constant`).
template <class T, T V> using IntegralConstant = std::integral_constant<T, V>;
/// `IntegralConstant<bool, true>`.
using TrueType = std::true_type;
/// `IntegralConstant<bool, false>`.
using FalseType = std::false_type;
/// Maps any list of types to `void` (`std::void_t`), for detection idioms.
template <class...> using VoidType = void;

/// True when `T` and `U` are the same type.
template <class T, class U> inline constexpr bool IsSame = std::is_same_v<T, U>;
/// True when `From` converts implicitly to `To`.
template <class From, class To> inline constexpr bool IsConvertible = std::is_convertible_v<From, To>;
/// True when `T` is constructible from `Args`.
template <class T, class... Args> inline constexpr bool IsConstructible = std::is_constructible_v<T, Args...>;
/// True when `T` is an integral type.
template <class T> inline constexpr bool IsIntegral = std::is_integral_v<T>;
/// True when `T` is a floating-point type.
template <class T> inline constexpr bool IsFloatingPoint = std::is_floating_point_v<T>;
/// True when `T` is a signed arithmetic type.
template <class T> inline constexpr bool IsSigned = std::is_signed_v<T>;
/// True when `T` is an unsigned arithmetic type.
template <class T> inline constexpr bool IsUnsigned = std::is_unsigned_v<T>;
/// True when `T` is an enumeration.
template <class T> inline constexpr bool IsEnum = std::is_enum_v<T>;
/// True when `T` is a pointer.
template <class T> inline constexpr bool IsPointer = std::is_pointer_v<T>;
/// True when `T` is const-qualified.
template <class T> inline constexpr bool IsConst = std::is_const_v<T>;
/// True when `T` is trivially copyable.
template <class T> inline constexpr bool IsTriviallyCopyable = std::is_trivially_copyable_v<T>;
/// True when `T` is trivially destructible.
template <class T> inline constexpr bool IsTriviallyDestructible = std::is_trivially_destructible_v<T>;
/// True when `T` is trivially default constructible.
template <class T> inline constexpr bool IsTriviallyDefaultConstructible = std::is_trivially_default_constructible_v<T>;
/// True when `T` is a standard-layout type.
template <class T> inline constexpr bool IsStandardLayout = std::is_standard_layout_v<T>;
/// True when `T` is move constructible.
template <class T> inline constexpr bool IsMoveConstructible = std::is_move_constructible_v<T>;
/// True when `T` is copy constructible.
template <class T> inline constexpr bool IsCopyConstructible = std::is_copy_constructible_v<T>;
/// True when `T` is nothrow move constructible.
template <class T> inline constexpr bool IsNothrowMoveConstructible = std::is_nothrow_move_constructible_v<T>;
/// True when `Base` is a base class of `Derived`.
template <class Base, class Derived> inline constexpr bool IsBaseOf = std::is_base_of_v<Base, Derived>;
/// True when `T` is an empty class.
template <class T> inline constexpr bool IsEmpty = std::is_empty_v<T>;
/// True when `T` is an lvalue or rvalue reference.
template <class T> inline constexpr bool IsReference = std::is_reference_v<T>;
/// True when `T` is an array type (bounded or not).
template <class T> inline constexpr bool IsArray = std::is_array_v<T>;
/// True when `T` is volatile-qualified.
template <class T> inline constexpr bool IsVolatile = std::is_volatile_v<T>;
/// True when an object of `T` can be destroyed (`T` is not an abstract class with a deleted destructor, an array of
/// unknown bound, ...).
template <class T> inline constexpr bool IsDestructible = std::is_destructible_v<T>;
/// True when `T` can be default constructed.
template <class T> inline constexpr bool IsDefaultConstructible = std::is_default_constructible_v<T>;
/// True when `T` has a virtual destructor.
template <class T> inline constexpr bool HasVirtualDestructor = std::has_virtual_destructor_v<T>;
/// `T` itself, in a position the compiler must not deduce from.
template <class T> using TypeIdentity = std::type_identity_t<T>;

/// `T` if `Condition`, otherwise ill-formed (`std::enable_if_t`).
template <bool Condition, class T = void> using EnableIf = std::enable_if_t<Condition, T>;
/// `T` if `Condition`, otherwise `F` (`std::conditional_t`).
template <bool Condition, class T, class F> using Conditional = std::conditional_t<Condition, T, F>;
/// `T` without a reference.
template <class T> using RemoveReference = std::remove_reference_t<T>;
/// `T` without const.
template <class T> using RemoveConst = std::remove_const_t<T>;
/// `T` without const and volatile.
template <class T> using RemoveCV = std::remove_cv_t<T>;
/// `T` without const, volatile and reference.
template <class T> using RemoveCVRef = std::remove_cv_t<std::remove_reference_t<T>>;
/// `T` without a pointer level.
template <class T> using RemovePointer = std::remove_pointer_t<T>;
/// `T` with its array and function decay applied, as for a by-value parameter.
template <class T> using Decay = std::decay_t<T>;
/// The integer type underlying the enumeration `T`.
template <class T> using UnderlyingType = std::underlying_type_t<T>;
/// The signed integer type of the same width as `T`.
template <class T> using MakeSigned = std::make_signed_t<T>;
/// The unsigned integer type of the same width as `T`.
template <class T> using MakeUnsigned = std::make_unsigned_t<T>;
/// `T` with a lvalue reference added.
template <class T> using AddLvalueReference = std::add_lvalue_reference_t<T>;
/// `T` with a pointer added.
template <class T> using AddPointer = std::add_pointer_t<T>;
/// The type of the result of calling a `F` with `Args`.
template <class F, class... Args> using InvokeResult = std::invoke_result_t<F, Args...>;
/// True when the call `F(Args...)` is well formed.
template <class F, class... Args> inline constexpr bool IsInvocable = std::is_invocable_v<F, Args...>;

/// True during constant evaluation (`std::is_constant_evaluated`).
constexpr bool IsConstantEvaluated() noexcept
{
    return std::is_constant_evaluated();
}

}  // namespace engine_placeholder
