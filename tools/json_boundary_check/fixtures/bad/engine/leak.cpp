#include <yyjson.h>
#include "third_party/sqlite/yyjson.h"
int Open() { yyjson* db = nullptr; return yyjson_open(":memory:", &db) == YYJSON_OK; }
