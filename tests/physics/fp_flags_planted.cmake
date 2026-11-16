# Run by fp_flags_check.cmake: a configure step with a fast-math flag in a per-configuration variable must fail.
include(${SOURCE_DIR}/cmake/EngineFpCheck.cmake)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ffast-math")
engine_fp_check_flags()
