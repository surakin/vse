#pragma once
// Planted violations of tools/shared_state_check (the line numbers are checked by the gate's own test).
struct Leak
{
    static int Count();
    static int shared;
    static inline int shared_inline = 0;
    static Leak& Instance()
    {
        static Leak instance;
        return instance;
    }
};
thread_local int t_depth = 0;
inline int g_counter = 0;
inline void Lookup()
{
    static Vector<const char*> names;
}
