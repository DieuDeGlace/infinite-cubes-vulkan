include(FetchContent)

FetchContent_Declare(vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG v1.4.304 GIT_SHALLOW TRUE)
FetchContent_Declare(volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG 1.4.304 GIT_SHALLOW TRUE)
FetchContent_Declare(vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG v3.3.0 GIT_SHALLOW TRUE SOURCE_SUBDIR unused)
set(VOLK_PULL_IN_VULKAN OFF)
FetchContent_MakeAvailable(vulkan_headers volk vma)
target_link_libraries(volk PUBLIC Vulkan::Headers)
target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(cubeworld SYSTEM PRIVATE "${vma_SOURCE_DIR}/include")
target_link_libraries(cubeworld PRIVATE volk::volk)

find_program(DXC_EXECUTABLE NAMES dxc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
if(DXC_EXECUTABLE)
  # Windows SDK builds of DXC can list -spirv in help and still lack the SPIR-V backend.
  set(dxc_probe "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/cubeworld-dxc-probe")
  file(CONFIGURE OUTPUT "${dxc_probe}.hlsl"
    CONTENT "float4 main() : SV_TARGET { return float4(1.0, 1.0, 1.0, 1.0); }\n")
  execute_process(
    COMMAND "${DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.3 -T ps_6_0 -E main
            -Fo "${dxc_probe}.spv" "${dxc_probe}.hlsl"
    RESULT_VARIABLE dxc_probe_result OUTPUT_QUIET ERROR_QUIET)
  if(NOT dxc_probe_result STREQUAL "0")
    message(STATUS "DXC at ${DXC_EXECUTABLE} cannot compile Vulkan SPIR-V. Using the pinned compiler.")
    set(DXC_EXECUTABLE "")
  endif()
endif()
if(NOT DXC_EXECUTABLE)
  set(dxc_release https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609)
  if(CMAKE_HOST_WIN32)
    FetchContent_Declare(dxc
      URL "${dxc_release}/dxc_2026_09_29.zip"
      URL_HASH SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
      SOURCE_SUBDIR unused)
    set(dxc_program bin/x64/dxc.exe)
  elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    FetchContent_Declare(dxc
      URL "${dxc_release}/linux_dxc_2026_09_28.x86_x64.tar.gz"
      URL_HASH SHA256=96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1
      SOURCE_SUBDIR unused)
    set(dxc_program bin/dxc)
  else()
    message(FATAL_ERROR "Set DXC_EXECUTABLE to a host DXC with SPIR-V support")
  endif()
  FetchContent_MakeAvailable(dxc)
  set(DXC_EXECUTABLE "${dxc_SOURCE_DIR}/${dxc_program}")
endif()

set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/generated/spirv")
file(MAKE_DIRECTORY "${shader_output}")
target_include_directories(cubeworld PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

function(add_vulkan_shader directory name profile)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/${directory}/${name}.hlsl")
  file(GLOB includes CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${directory}/*.hlsli")
  set(binary "${shader_output}/${name}.spv")
  set(header "${shader_output}/${name}.h")
  set(embed "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedSpirv.py")
  if(ANDROID AND name STREQUAL "WorldPixelShader")
    set(defines -DCUBE_MOBILE_SHADOWS=1)
  endif()
  add_custom_command(OUTPUT "${header}" BYPRODUCTS "${binary}"
    COMMAND "${DXC_EXECUTABLE}" -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout
            ${defines} -T "${profile}" -E main -Fo "${binary}" "${source}"
    COMMAND "${Python3_EXECUTABLE}" "${embed}" "${binary}" "${header}" "${name}"
    DEPENDS "${source}" ${includes} "${embed}" "${DXC_EXECUTABLE}"
    VERBATIM)
  target_sources(cubeworld PRIVATE "${header}")
endfunction()

set(world_shaders src/gfx/shaders)
add_vulkan_shader(${world_shaders} WorldVertexShader vs_6_0)
add_vulkan_shader(${world_shaders} WaterVertexShader vs_6_0)
add_vulkan_shader(${world_shaders} ShadowVertexShader vs_6_0)
add_vulkan_shader(${world_shaders} ReflectiveShadowVertexShader vs_6_0)
add_vulkan_shader(${world_shaders} ReflectiveShadowPixelShader ps_6_0)
add_vulkan_shader(${world_shaders} PostVertexShader vs_6_0)
add_vulkan_shader(${world_shaders} TexturedVertexShader vs_6_0)
foreach(name WorldPixelShader SkyPixelShader WaterPixelShader CloudPixelShader TexturedPixelShader)
  add_vulkan_shader(${world_shaders} ${name} ps_6_0)
endforeach()
foreach(name PostCopyShader TemporalResolveShader SmaaEdgesShader SmaaWeightsShader SmaaBlendShader)
  add_vulkan_shader(${world_shaders} ${name} ps_6_0)
endforeach()
set(ui_shaders plasma/src/plasma/shaders)
add_vulkan_shader(${ui_shaders} InterfaceVertexShader vs_6_0)
add_vulkan_shader(${ui_shaders} InterfacePixelShader ps_6_0)
