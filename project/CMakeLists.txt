# Source files
set(SOURCES
    "src/app.cpp"
    "src/camera.cpp"
    "src/command_context.cpp"
    "src/frame_sync.cpp"
    "src/gltf_loader.cpp"
    "src/ibl.cpp"
    "src/main.cpp"
    "src/material.cpp"
    "src/mesh.cpp"
    "src/pipeline_builder.cpp"
    "src/renderer.cpp"
    "src/render_targets.cpp"
    "src/resource_manager.cpp"
    "src/scene.cpp"
    "src/swapchain.cpp"
    "src/transform.cpp"
    "src/vk_utils.cpp"
    "src/vulkan_context.cpp"
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})



# -------------------------------------------------------------------
# Dependencies
# -------------------------------------------------------------------

include(FetchContent)

find_package(Vulkan REQUIRED)

# Fetch GLFW
FetchContent_Declare(
    glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz
)
FetchContent_MakeAvailable(glfw)

# Fetch GLM
FetchContent_Declare(
    glm
    URL https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz
)
FetchContent_MakeAvailable(glm)

# Fetch STB
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 31c1ad37456438565541f4919958214b6e762fb4
)

FetchContent_MakeAvailable(stb)

add_library(stb_image INTERFACE)

target_include_directories(stb_image INTERFACE
    ${stb_SOURCE_DIR}
)

# Fetch VMA
FetchContent_Declare(
    VMA
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG b3cbbb43ea3a506dffe10759e205a41c27c35ae2
)

FetchContent_MakeAvailable(VMA)

add_library(vma_lib INTERFACE)

target_include_directories(vma_lib INTERFACE
    "${vma_SOURCE_DIR}/include"
)



# Fetch TinyGLTF
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.6
)

set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinygltf)



target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glfw
        Vulkan::Vulkan
        glm
        stb_image
        tinygltf
        vma_lib
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${Vulkan_INCLUDE_DIRS}"
)


# -------------------------------------------------------------------
# Copy non-shader resources to output folder
# -------------------------------------------------------------------

set(RESOURCES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(RESOURCES_OUT_DIR    "${CMAKE_CURRENT_BINARY_DIR}/resources/")

file(MAKE_DIRECTORY ${RESOURCES_OUT_DIR})

file(GLOB_RECURSE RESOURCE_FILES
    "${RESOURCES_SOURCE_DIR}/*.jpg"
    "${RESOURCES_SOURCE_DIR}/*.jpeg"
    "${RESOURCES_SOURCE_DIR}/*.png"
    "${RESOURCES_SOURCE_DIR}/*.obj"
    "${RESOURCES_SOURCE_DIR}/*.fx"
    "${RESOURCES_SOURCE_DIR}/*.glb"
    "${RESOURCES_SOURCE_DIR}/*.bin"
    "${RESOURCES_SOURCE_DIR}/*.gltf"
    "${RESOURCES_SOURCE_DIR}/*.hdr"
)

foreach(RESOURCE ${RESOURCE_FILES})
    # Get path relative to resources root
    file(RELATIVE_PATH RESOURCE_REL "${RESOURCES_SOURCE_DIR}" "${RESOURCE}")

    # Full destination path (preserves subfolders)
    set(RESOURCE_DEST "${RESOURCES_OUT_DIR}/${RESOURCE_REL}")

    # Ensure destination directory exists
    get_filename_component(RESOURCE_DEST_DIR "${RESOURCE_DEST}" DIRECTORY)
    
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCE_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${RESOURCE}" "${RESOURCE_DEST}"
        COMMENT "Copying resource: ${RESOURCE_REL}"
        VERBATIM
    )
endforeach()



# -------------------------------------------------------------------
# Shader compilation (GLSL -> SPIR-V via glslangValidator)
# -------------------------------------------------------------------

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

set(SHADERS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders")
set(SHADERS_OUT_DIR    "${CMAKE_CURRENT_BINARY_DIR}/resources/shaders")

file(MAKE_DIRECTORY ${SHADERS_OUT_DIR})

file(GLOB_RECURSE SHADER_SOURCES
    "${SHADERS_SOURCE_DIR}/*.vert"
    "${SHADERS_SOURCE_DIR}/*.frag"
    "${SHADERS_SOURCE_DIR}/*.comp"
    "${SHADERS_SOURCE_DIR}/*.geom"
    "${SHADERS_SOURCE_DIR}/*.tesc"
    "${SHADERS_SOURCE_DIR}/*.tese"
    "${SHADERS_SOURCE_DIR}/*.glsl"
)

set(SPIRV_OUTPUTS "")

foreach(SHADER_SRC ${SHADER_SOURCES})
    # Derive output .spv path, mirroring the source tree under the binary dir
    file(RELATIVE_PATH SHADER_REL "${SHADERS_SOURCE_DIR}" "${SHADER_SRC}")
    set(SHADER_SPV "${SHADERS_OUT_DIR}/${SHADER_REL}.spv")

    # Ensure the subdirectory exists for nested shader layouts
    get_filename_component(SHADER_SPV_DIR "${SHADER_SPV}" DIRECTORY)
    file(MAKE_DIRECTORY "${SHADER_SPV_DIR}")

    add_custom_command(
        OUTPUT  "${SHADER_SPV}"
        COMMAND "${GLSLANG_VALIDATOR}" -V "${SHADER_SRC}" -o "${SHADER_SPV}"
        DEPENDS "${SHADER_SRC}"
        COMMENT "Compiling shader: ${SHADER_REL}"
        VERBATIM
    )

    list(APPEND SPIRV_OUTPUTS "${SHADER_SPV}")
endforeach()

# Create a custom target that depends on all compiled shaders,
# then make the main executable depend on it so shaders always build first
add_custom_target(${PROJECT_NAME}_Shaders ALL DEPENDS ${SPIRV_OUTPUTS})
add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_Shaders)