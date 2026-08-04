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

set(COMPUTE_SPV_A ${CMAKE_SOURCE_DIR}/shaders/compute_a.spv)
set(COMPUTE_SPV_B ${CMAKE_SOURCE_DIR}/shaders/compute_b.spv)

# Complile shaders at build for now
add_custom_command(
    OUTPUT ${COMPUTE_SPV_A}
    COMMAND ${DXC_EXECUTABLE} -spirv -T cs_6_0 -E main ${CMAKE_SOURCE_DIR}/shaders/shaderA.hlsl -Fo ${COMPUTE_SPV_A}
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/shaderA.hlsl
    COMMENT "Compiling shaderA.hlsl to compute_a.spv"
)
add_custom_command(
    OUTPUT ${COMPUTE_SPV_B}
    COMMAND ${DXC_EXECUTABLE} -spirv -T cs_6_0 -E main ${CMAKE_SOURCE_DIR}/shaders/shaderB.hlsl -Fo ${COMPUTE_SPV_B}
    DEPENDS ${CMAKE_SOURCE_DIR}/shaders/shaderB.hlsl
    COMMENT "Compiling shaderB.hlsl to compute_b.spv"
)

add_custom_target(shaders DEPENDS ${COMPUTE_SPV_A} ${COMPUTE_SPV_B})
add_dependencies(literay shaders)