#include "database/sql_text.h"
void Leak(vse::StringView s) { auto t = vse::database::SqlText::Unchecked(s); }
// a range [0, n) opens a bracket
void Leak2(vse::StringView s) { auto t = vse::database::SqlText::Unchecked(s); }
