# shader_base_symbol(<file name like triangle.vert> <out var>) -> kTriangleVert, the name shader_cook is given; it
# appends Spv, SpvWordCount, Refl and ReflByteCount for the arrays in the two generated files.
function(shader_base_symbol shader out)
  string(REGEX REPLACE "[._]" ";" parts "${shader}")
  set(symbol "k")
  foreach(part ${parts})
    string(SUBSTRING "${part}" 0 1 first)
    string(SUBSTRING "${part}" 1 -1 rest)
    string(TOUPPER "${first}" first)
    string(APPEND symbol "${first}${rest}")
  endforeach()
  set(${out} "${symbol}" PARENT_SCOPE)
endfunction()

# shader_stage(<file name like triangle.vert> <out var>) -> the slangc stage name: vertex, fragment or compute.
function(shader_stage shader out)
  if(shader MATCHES "\\.vert$")
    set(stage vertex)
  elseif(shader MATCHES "\\.frag$")
    set(stage fragment)
  elseif(shader MATCHES "\\.comp$")
    set(stage compute)
  else()
    message(FATAL_ERROR "cannot tell the shader stage of '${shader}' (expected .vert, .frag or .comp)")
  endif()
  set(${out} "${stage}" PARENT_SCOPE)
endfunction()

# slang_compile_args(<stage> <out var>) -> the slangc arguments every engine shader is built with (see README.md).
function(slang_compile_args stage out)
  set(${out} -target spirv -profile spirv_1_5 -entry main -stage ${stage} -matrix-layout-column-major -O2 PARENT_SCOPE)
endfunction()
