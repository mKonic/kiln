find_program(GLSLC glslc REQUIRED)
# kiln_embed_shaders(target dir): every *.vert/*.frag/*.comp in dir becomes kiln::shaders::kName[]
# in <build>/gen/kiln/Shaders.h, regenerated when a shader changes.
function(kiln_embed_shaders target dir)
    file(GLOB shaders CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.vert
         ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.frag ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.comp)
    set(out ${CMAKE_BINARY_DIR}/gen/kiln/Shaders.h)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E env GLSLC=${GLSLC}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-shaders.sh ${out} ${shaders}
        DEPENDS ${shaders} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed-shaders.sh
        COMMENT "Embedding kiln shaders")
    add_custom_target(${target}_shaders DEPENDS ${out})
    add_dependencies(${target} ${target}_shaders)
    target_include_directories(${target} PUBLIC ${CMAKE_BINARY_DIR}/gen)
endfunction()
