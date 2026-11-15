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
# Day 363: a skeleton (a01: root, arm) with two clips (c01 moves the arm from y 1 to 2, c03 holds it at 3), another
# skeleton (a02) with its own clip (c02), and a skinned mesh in the full layout (b05) bound to the first.
file(COPY ${SAMPLE}/src/skeletons/hero.skel.src DESTINATION ${OUT})
file(COPY ${SAMPLE}/src/clips/idle.anim.src DESTINATION ${OUT})
file(WRITE ${OUT}/other.skel.src "id 0000000000000a02\njoint root - 0 0 0  0 0 0 1  1 1 1\n")
file(WRITE ${OUT}/other.anim.src "id 0000000000000c02\nskeleton 0000000000000a02\nduration 1\nrate 30\nflags loop\ntrack root\nt 0 0 0 0\nt 1 0 1 0\n")
file(WRITE ${OUT}/hold.anim.src "id 0000000000000c03\nskeleton 0000000000000a01\nduration 1\nrate 30\nflags loop\ntrack arm\nt 0 0 3 0\nt 1 0 3 0\n")
file(WRITE ${OUT}/skinned.mesh.src "id 0000000000000b05\nquality full\nskeleton 0000000000000a01\nv -1 -1 0  0 0 1  1 0 0 1  0 0\nv 1 -1 0  0 0 1  1 0 0 1  1 0\nv 0 1 0  0 0 1  1 0 0 1  0.5 1\ni 0 1 2\nskin 0 0 1\nskin 1 0 1\nskin 2 1 1\n")
mint(mint-skeleton hero.skel.src hero.skel.blob)
mint(mint-skeleton other.skel.src other.skel.blob)
mint(mint-clip idle.anim.src idle.anim.blob --skeleton=hero.skel.blob)
mint(mint-clip other.anim.src other.anim.blob --skeleton=other.skel.blob)
mint(mint-clip hold.anim.src hold.anim.blob --skeleton=hero.skel.blob)
mint(mint-mesh skinned.mesh.src skinned.mesh.blob --skeleton=hero.skel.blob)
file(WRITE ${OUT}/done.txt "minted\n")
