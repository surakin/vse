#ifndef OK_H
#define OK_H
#include <stdbool.h>
/* class namespace template in a comment are fine; so is "std::string" in a string */
#ifdef __cplusplus
extern "C" {
#endif
bool engine_ok(int x);
#ifdef __cplusplus
}
#endif
#endif
