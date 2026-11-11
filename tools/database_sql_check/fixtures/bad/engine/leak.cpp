#include "database/sql_text.h"
void Leak(engine_placeholder::StringView s) { auto t = engine_placeholder::database::SqlText::Unchecked(s); }
