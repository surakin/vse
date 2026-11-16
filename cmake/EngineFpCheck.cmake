# The floating-point flag check (Phase 25 Day 374). The physics step is deterministic only while the compiler neither contracts a * b + c to a
# fused multiply-add nor reassociates (docs/superpowers/specs/2026-11-16-physics-design.md, section 3). EngineCompilerOptions.cmake sets the flags
# that forbid it; this file stops the configure step when anything else turns it back on.
#
#   engine_fp_flag_problem(<out> <flags string>): <out> receives the first forbidden flag found in the string, or "".
#   engine_fp_check_flags(): checks CMAKE_C/CXX_FLAGS (and the per-configuration and linker variants) and fails with FATAL_ERROR.
#
# Forbidden: -ffast-math, -Ofast, -funsafe-math-optimizations, -fassociative-math, -freciprocal-math, -ffinite-math-only, -fno-signed-zeros,
# -ffp-contract=fast and =on, -mrecip, /fp:fast, /fp:contract and /fp:except- (the last two only widen what the compiler may do). A linker flag
# of -ffast-math is forbidden too: it links crtfastmath.o, which sets flush-to-zero for the whole process before main.

function(engine_fp_flag_problem out flags)
  set(forbidden
    "-ffast-math" "-Ofast" "-funsafe-math-optimizations" "-fassociative-math" "-freciprocal-math" "-ffinite-math-only"
    "-fno-signed-zeros" "-ffp-contract=fast" "-ffp-contract=on" "-mrecip" "/fp:fast" "/fp:contract" "-fp-model=fast")
  set(found "")
  separate_arguments(list NATIVE_COMMAND "${flags}")
  foreach(flag ${list})
    foreach(bad ${forbidden})
      if(flag STREQUAL bad)
        set(found "${bad}")
        break()
      endif()
    endforeach()
    if(found)
      break()
    endif()
  endforeach()
  set(${out} "${found}" PARENT_SCOPE)
endfunction()

function(engine_fp_check_flags)
  set(variables CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
  foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    list(APPEND variables CMAKE_C_FLAGS_${config} CMAKE_CXX_FLAGS_${config} CMAKE_EXE_LINKER_FLAGS_${config})
  endforeach()
  foreach(variable ${variables})
    if(DEFINED ${variable})
      engine_fp_flag_problem(problem "${${variable}}")
      if(problem)
        message(FATAL_ERROR "${variable} contains ${problem}: the physics step must be bit-identical on every machine, and this flag lets the compiler "
          "change floating-point results (docs/superpowers/specs/2026-11-16-physics-design.md, section 3). Remove it.")
      endif()
    endif()
  endforeach()
endfunction()
