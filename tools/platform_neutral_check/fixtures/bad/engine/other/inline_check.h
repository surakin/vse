#pragma once
// Planted: configuration-dependent checks in header-inline code. This comment mentions ENGINE_VERIFY( harmlessly.
inline int Twice(int v) { ENGINE_VERIFY(v > 0, "positive"); return v * 2; }
inline void Slow(int v) { ENGINE_CHECK_SLOW(v > 0, "positive"); }
inline int Pick(int v) { switch (v) { case 0: return 1; default: ENGINE_UNREACHABLE("other"); } }
