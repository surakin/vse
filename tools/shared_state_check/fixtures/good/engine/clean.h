#pragma once
// Nothing here defines mutable state: constants, functions, declarations and a member declared here and defined in the .cpp.
constexpr int kLimit = 4;
inline constexpr int kOther = 5;
struct Clean
{
    static constexpr int kSize = 8;
    static const int kName = 3;
    static int Count();
    static Clean& Instance();
    static void* Make(unsigned long size, unsigned long align);
    // static int in_a_comment = 0;
    const char* text = "static int in_a_string = 0;";
};
inline int Twice(int x)
{
    return x * 2;
}
inline int Pick()
{
    static const int kTable[2] = {1, 2};
    static const char* const kName = "x";
    return kTable[0] + (kName != nullptr);
}
