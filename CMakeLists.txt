cmake_minimum_required(VERSION 3.15)

project(literay LANGUAGES C)

find_package(Vulkan REQUIRED)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/$<CONFIG>)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.c")

add_executable(literay ${SOURCES})

target_compile_features(literay PRIVATE c_std_11)
target_include_directories(literay PRIVATE ${CMAKE_SOURCE_DIR}/include ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(literay PRIVATE literay_PROJECT_DIR=\"${CMAKE_SOURCE_DIR}\")
target_link_libraries(literay PRIVATE glfw ${Vulkan_LIBRARIES})

if(WIN32)
    target_compile_definitions(literay PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()


find_program(DXC_EXECUTABLE NAMES dxc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc not found; install the Vulkan SDK or set VULKAN_SDK")
endif()

set(COMPUTE_SPV ${CMAKE_SOURCE_DIR}/shaders/compute.spv)

# Complile shaders at build for now
add_custom_command(
    OUTPUT ${COMPUTE_SPV}
    COMMAND ${DXC_EXECUTABLE} -spirv -T cs_6_0 -E main ${CMAKE_SOURCE_DIR}/shaders/shader.hlsl -Fo ${COMPUTE_SPV}
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/shader.hlsl
    COMMENT "Compiling shader.hlsl to compute.spv"
)

add_custom_target(shaders DEPENDS ${COMPUTE_SPV})
add_dependencies(literay shaders)