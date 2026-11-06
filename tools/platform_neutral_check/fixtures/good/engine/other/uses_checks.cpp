// Allowed: a source file may use the per-translation-unit macros.
void Check(int v) { ENGINE_VERIFY(v > 0, "positive"); ENGINE_CHECK_SLOW(v > 0, "positive"); }
