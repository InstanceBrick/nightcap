cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NightcapFidelityFX LANGUAGES CXX)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Vulkan REQUIRED)
find_program(FSR3_GLSLANG_VALIDATOR glslangValidator REQUIRED)
find_program(FSR3_SPIRV_VALIDATOR spirv-val)
set(FSR3_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(FSR3_SHADER_BLOBS ${FSR3_GENERATED_DIR}/fsr3_shader_blobs.cpp)
set(FSR3_OF_SHADER_BLOBS ${FSR3_GENERATED_DIR}/opticalflow_shader_blobs.cpp)
file(GLOB_RECURSE FSR3_SHADER_INPUTS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/include/FidelityFX/gpu/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/src/backends/vk/shaders/fsr3upscaler/*.glsl)
file(GLOB FSR3_OF_SHADER_INPUTS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/src/backends/vk/shaders/opticalflow/*.glsl)
set(FSR3_VALIDATOR_ARGS)
if(FSR3_SPIRV_VALIDATOR)
    list(APPEND FSR3_VALIDATOR_ARGS --validator ${FSR3_SPIRV_VALIDATOR})
endif()
add_custom_command(OUTPUT ${FSR3_SHADER_BLOBS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fsr3_shaders.py
        --sdk ${CMAKE_CURRENT_SOURCE_DIR}/sdk --compiler ${FSR3_GLSLANG_VALIDATOR}
        --output ${FSR3_GENERATED_DIR} ${FSR3_VALIDATOR_ARGS}
    DEPENDS ${FSR3_SHADER_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fsr3_shaders.py
    VERBATIM)
add_custom_command(OUTPUT ${FSR3_OF_SHADER_BLOBS} ${FSR3_GENERATED_DIR}/opticalflow_motion_spv.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compile_opticalflow_shaders.py
        --sdk ${CMAKE_CURRENT_SOURCE_DIR}/sdk --compiler ${FSR3_GLSLANG_VALIDATOR}
        --output ${FSR3_GENERATED_DIR} ${FSR3_VALIDATOR_ARGS}
    DEPENDS ${FSR3_SHADER_INPUTS} ${FSR3_OF_SHADER_INPUTS}
        ${CMAKE_CURRENT_SOURCE_DIR}/compile_opticalflow_shaders.py
        ${CMAKE_CURRENT_SOURCE_DIR}/compile_fsr3_shaders.py
        ${CMAKE_CURRENT_SOURCE_DIR}/opticalflow_to_motion.comp
    VERBATIM)
add_custom_command(OUTPUT ${FSR3_GENERATED_DIR}/fsr3_gaze_spv.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/compile_fsr3_gaze_shaders.py
        --compiler ${FSR3_GLSLANG_VALIDATOR} --output ${FSR3_GENERATED_DIR} ${FSR3_VALIDATOR_ARGS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compile_fsr3_gaze_shaders.py
        ${CMAKE_CURRENT_SOURCE_DIR}/fsr3_gaze_crop.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/fsr3_gaze_composite.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/fsr3_gaze_composite.frag
    VERBATIM)
add_library(nightcap_fsr3 STATIC
    sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
    sdk/src/components/opticalflow/ffx_opticalflow.cpp
    sdk/src/shared/ffx_assert.cpp
    sdk/src/shared/ffx_breadcrumbs_list.cpp
    sdk/src/shared/ffx_message.cpp
    sdk/src/shared/ffx_object_management.cpp
    sdk/src/backends/vk/ffx_vk.cpp
    sdk/src/backends/vk/ffx_vk_private_loader.cpp
    fsr3_vulkan_support.cpp
    ${FSR3_SHADER_BLOBS} ${FSR3_OF_SHADER_BLOBS}
    ${FSR3_GENERATED_DIR}/fsr3_gaze_spv.h)
target_compile_features(nightcap_fsr3 PUBLIC cxx_std_17)
target_include_directories(nightcap_fsr3 PUBLIC sdk/include ${FSR3_GENERATED_DIR} PRIVATE
    sdk/src/shared sdk/src/backends/shared)
target_compile_definitions(nightcap_fsr3 PUBLIC FFX_GCC=1)
if(NOT WIN32)
    target_compile_options(nightcap_fsr3 PRIVATE
        -include ${CMAKE_CURRENT_SOURCE_DIR}/ffx_linux_compat.h)
endif()
target_link_libraries(nightcap_fsr3 PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})
set_target_properties(nightcap_fsr3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
option(FSR3_BUILD_VULKAN_SMOKE "Build the standalone FSR 3.1 Vulkan dispatch check" OFF)
if(FSR3_BUILD_VULKAN_SMOKE)
    add_executable(fsr3_vulkan_smoke fsr3_vulkan_smoke.cpp
        ../../src/vr/fsr3_upscaler.cc)
    target_include_directories(fsr3_vulkan_smoke PRIVATE ../../include)
    target_link_libraries(fsr3_vulkan_smoke PRIVATE nightcap_fsr3 Vulkan::Vulkan)
endif()
option(FSR3_BUILD_TRANSPORT_SMOKE "Build the encoded color transport GPU check" OFF)
if(FSR3_BUILD_TRANSPORT_SMOKE)
    add_executable(fsr3_transport_smoke fsr3_transport_smoke.cpp
        ../../src/vr/fsr3_upscaler.cc ../../src/vr/fsr3_transport.cc)
    target_include_directories(fsr3_transport_smoke PRIVATE ../../include)
    target_link_libraries(fsr3_transport_smoke PRIVATE nightcap_fsr3 Vulkan::Vulkan)
endif()

option(FSR3_BUILD_GAZE_SMOKE "Build the gaze-region temporal upscaling GPU check" OFF)
if(FSR3_BUILD_GAZE_SMOKE)
    add_executable(fsr3_gaze_transport_smoke fsr3_gaze_transport_smoke.cpp
        ../../src/vr/fsr3_upscaler.cc ../../src/vr/fsr3_gaze_transport.cc)
    target_include_directories(fsr3_gaze_transport_smoke PRIVATE ../../include)
    target_link_libraries(fsr3_gaze_transport_smoke PRIVATE nightcap_fsr3 Vulkan::Vulkan)
endif()
