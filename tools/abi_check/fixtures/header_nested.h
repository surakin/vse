#ifndef N_H
#define N_H
#ifdef __cplusplus
extern "C" {
#ifdef X
#define Y 1
#endif
namespace inside_cpp {}
#else
int c_branch(int x);
#endif
#endif
