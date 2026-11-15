# Day 350: the blobs the resource host test adds from memory, minted by asset_mint (TOOL) from the sample sources of tests/asset/mint_sample (SAMPLE) into
# OUT: a texture (0d01), a second version of it with the same id and different content (mips 1), a material (0e01) that depends on the texture, and
# a mesh (0b02). Run by a custom command of tests/engine_library/CMakeLists.txt.
file(MAKE_DIRECTORY ${OUT})
file(COPY ${SAMPLE}/src/textures/brick.raw DESTINATION ${OUT})
file(COPY ${SAMPLE}/src/textures/brick.tex.src DESTINATION ${OUT})
file(WRITE ${OUT}/brick_b.tex.src "id 0000000000000d01\nsource brick.raw\nformat rgba8\nsrgb true\nmips 1\n")
file(WRITE ${OUT}/brick.mat.src "id 0000000000000e01\ntemplate gbuffer\nparam roughness float 0.5\ntexture albedo_tex brick.tex.blob\n")
file(COPY ${SAMPLE}/src/meshes/rock.mesh.src DESTINATION ${OUT})
# Day 359: a mesh in the full vertex layout, which the G-buffer templates read (the packed default does not match them).
file(WRITE ${OUT}/scene.mesh.src "id 0000000000000b03\nquality full\nv -1 -1 0  0 0 1  1 0 0 1  0 0\nv 1 -1 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0.5 1\ni 0 1 2\n")

function(mint)
  execute_process(COMMAND ${TOOL} ${ARGN} WORKING_DIRECTORY ${OUT} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "asset_mint ${ARGN} failed (${result}): ${output}")
  endif()
endfunction()

mint(mint-texture brick.tex.src brick.tex.blob)
mint(mint-texture brick_b.tex.src brick_b.tex.blob)
mint(mint-material brick.mat.src brick.mat.blob)
mint(mint-mesh rock.mesh.src rock.mesh.blob)
mint(mint-mesh scene.mesh.src scene.mesh.blob)
file(WRITE ${OUT}/late.mesh.src "id 0000000000000b04\nquality full\nv -1 -1 0  0 0 1  1 0 0 1  0 0\nv 1 -1 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0.5 1\ni 0 1 2\n")
mint(mint-mesh late.mesh.src late.mesh.blob)
file(WRITE ${OUT}/done.txt "minted\n")
