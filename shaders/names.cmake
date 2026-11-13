# shader_base_symbol(<file name like triangle.vert> <out var>) -> kTriangleVert, the name shader_mint is given; it
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

# slang_metal_args(<stage> <out var>) -> the slangc arguments of the Metal build (Phase 21 Day 299): Metal Shading Language with
# the same entry and matrix layout and the Metal target define (shaders/target_common.slang). Its reflection gives the Metal slots
# on every platform; on a Mac tools/metal_mint compiles the source into the committed .metallib.
function(slang_metal_args stage out)
  set(${out} -target metal -entry main -stage ${stage} -matrix-layout-column-major -O2 -DENGINE_TARGET_METAL=1 PARENT_SCOPE)
endfunction()

# slang_dxil_args(<stage> <dxc dir> <out var>) -> the slangc arguments of the DXIL build (Phase 13 Day 163): shader model 6.0, the
# same entry and matrix layout, the D3D12 target define (shaders/target_common.slang), and the directory that holds the dxcompiler.dll
# and dxil.dll slangc must use (the Windows SDK's, never the first dxc on PATH; dxil.dll signs the container).
function(slang_dxil_args stage dxc_dir out)
  set(${out} -target dxil -profile sm_6_0 -entry main -stage ${stage} -matrix-layout-column-major -O2 -DENGINE_TARGET_D3D12=1
             -dxc-path ${dxc_dir} PARENT_SCOPE)
endfunction()
