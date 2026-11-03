#pragma once

// Strings and chars that look like comments or braces must not confuse the scanner.
inline const char* kUrl = "http://example.com/a//b";
inline const char* kBlock = "/* not a comment */";
inline const char* kBrace = "}{ unbalanced in a string";
inline char kSlash = '/';
inline char kStar = '*';
inline char kQuote = '\'';
inline const char* kEscaped = "quote \" and backslash \\ and // slash";

inline const char* kRaw = R"delim(raw ) " string with } brace and // slash)delim";
inline const char* kRaw2 = R"(plain raw)";

inline int kBig = 1'000'000;
inline int kHex = 0xFF'FF;

#if 0
this is not valid C++ at all { unbalanced
/// A doc comment inside a skipped block must not attach.
void Skipped(
#endif

#define LONG_MACRO(a, b) \
    do {                 \
        (void)(a);       \
        (void)(b);       \
    } while (0)

/// A function after all the traps.
void AfterTraps();
