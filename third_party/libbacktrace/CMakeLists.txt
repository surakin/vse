# libbacktrace (https://github.com/ianlancetaylor/libbacktrace), vendored as source: see VENDORED.md and PLAN.md
# assumption #20. Upstream builds with autotools; this file replaces that for the platforms we build on. It produces one
# static library, `backtrace`, built once with plain flags and linked into the plain, ASan and TSan flavors alike.
#
# Only the ELF/Linux configuration exists so far: macOS (Mach-O, Phase 9) and Windows (PE/COFF, Phase 8) add a branch.
cmake_minimum_required(VERSION 3.20)

include(CheckCSourceCompiles)
include(CheckIncludeFile)
include(CheckSymbolExists)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "libbacktrace is configured for Linux only so far; macOS is Phase 9 and Windows Phase 8 (see third_party/libbacktrace/VENDORED.md)")
endif()

# --- the feature checks the sources test (what autoconf's configure.ac does) -----------------------------------------------
set(CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_include_file(link.h HAVE_LINK_H)
check_include_file(sys/link.h HAVE_SYS_LINK_H)
check_symbol_exists(dl_iterate_phdr "link.h" HAVE_DL_ITERATE_PHDR)
check_symbol_exists(readlink "unistd.h" HAVE_READLINK)
check_symbol_exists(lstat "sys/stat.h" HAVE_LSTAT)
check_symbol_exists(fcntl "fcntl.h" HAVE_FCNTL)
check_symbol_exists(strnlen "string.h" HAVE_DECL_STRNLEN_SYMBOL)
check_symbol_exists(getpagesize "unistd.h" HAVE_DECL_GETPAGESIZE_SYMBOL)
check_c_source_compiles("
#include <unwind.h>
int main(void) { int before = 0; return (int)_Unwind_GetIPInfo(0, &before); }" HAVE_GETIPINFO)
check_c_source_compiles("
int main(int argc, char** argv) {
  int v = 0;
  int* p = &v;
  __atomic_store_n(p, 1, __ATOMIC_RELEASE);
  return (int)__atomic_load_n(p, __ATOMIC_ACQUIRE) - 1 + (argc > 5 && argv != 0);
}" HAVE_ATOMIC_FUNCTIONS)
check_c_source_compiles("
int main(void) {
  int v = 0;
  __sync_bool_compare_and_swap(&v, 0, 1);
  return __sync_fetch_and_add(&v, 1) - 1;
}" HAVE_SYNC_FUNCTIONS)
unset(CMAKE_REQUIRED_DEFINITIONS)

set(HAVE_DECL_STRNLEN 0)
if(HAVE_DECL_STRNLEN_SYMBOL)
  set(HAVE_DECL_STRNLEN 1)
endif()
set(HAVE_DECL_GETPAGESIZE 0)
if(HAVE_DECL_GETPAGESIZE_SYMBOL)
  set(HAVE_DECL_GETPAGESIZE 1)
endif()
math(EXPR BACKTRACE_ELF_SIZE "${CMAKE_SIZEOF_VOID_P} * 8")

# backtrace-supported.h: unwinding through the compiler's unwinder (_Unwind_Backtrace), memory from mmap (so the
# resolving calls never call malloc and can run in a signal handler once the first call is made), threads supported,
# symbol lookup for data available (ELF).
if(NOT HAVE_DL_ITERATE_PHDR OR NOT HAVE_GETIPINFO)
  message(FATAL_ERROR "libbacktrace: dl_iterate_phdr and _Unwind_GetIPInfo are required on Linux")
endif()
set(BACKTRACE_SUPPORTED 1)
set(BACKTRACE_USES_MALLOC 0)
set(BACKTRACE_SUPPORTS_THREADS 1)
set(BACKTRACE_SUPPORTS_DATA 1)

set(BACKTRACE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(config.h.in ${BACKTRACE_GENERATED_DIR}/config.h)
configure_file(backtrace-supported.h.in ${BACKTRACE_GENERATED_DIR}/backtrace-supported.h @ONLY)

# ELF format, unwinder-based backtrace, mmap-based memory and file views.
add_library(backtrace STATIC
  atomic.c
  dwarf.c
  fileline.c
  posix.c
  print.c
  sort.c
  state.c
  backtrace.c
  simple.c
  elf.c
  mmap.c
  mmapio.c
)
target_include_directories(backtrace
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${BACKTRACE_GENERATED_DIR})
target_compile_definitions(backtrace PRIVATE _GNU_SOURCE HAVE_CONFIG_H)
# Third-party C code: its own flags, not ours (no -Werror; the engine's C++ options do not apply), and never merged into a
# unity translation unit (its files share static helper names).
set_target_properties(backtrace PROPERTIES UNITY_BUILD OFF POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
