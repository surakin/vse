#pragma once

//! Namespaces in every supported form.

namespace outer {
namespace inner {
/// A function in a nested namespace.
void InInner();
} // namespace inner

namespace detail {
/// Not part of the public surface (namespace named detail).
void Hidden();
} // namespace detail

namespace {
/// Not part of the public surface (anonymous namespace).
void FileLocal();
} // namespace

inline namespace v1 {
/// A function in an inline namespace.
void Versioned();
} // namespace v1
} // namespace outer

namespace a::b::c {
/// A function in a C++17 nested namespace.
void Deep();
} // namespace a::b::c

extern "C" {
/// A C-linkage function.
int c_entry(int value);
}
