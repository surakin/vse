#include "database/sql_text.h"
void Leak(engine_placeholder::StringView s) { auto t = engine_placeholder::database::SqlText::Unchecked(s); }
// a range [0, n) opens a bracket
void Leak2(engine_placeholder::StringView s) { auto t = engine_placeholder::database::SqlText::Unchecked(s); }
