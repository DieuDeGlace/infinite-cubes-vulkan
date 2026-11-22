set(release_root "${CMAKE_CURRENT_SOURCE_DIR}/../release")
add_library(cubeworld STATIC
  "${release_root}/src/gfx/VulkanContext.cpp"
  src/gfx/AlphaRenderDevice.cpp
  src/gfx/AlphaPipeline.cpp)
add_library(AlphaGraphics ALIAS cubeworld)
target_include_directories(cubeworld PUBLIC include plasma/include "${release_root}/include")
target_compile_definitions(cubeworld PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
include("${release_root}/cmake/Vulkan.cmake")
target_link_libraries(cubeworld PUBLIC volk::volk PRIVATE FreeImage)
target_include_directories(cubeworld PRIVATE "${release_root}/src/gfx")
set(quest_icon_atlas "${release_root}/thirdparty/overworldevents/quest-icons.png")
set(quest_icon_script "${release_root}/cmake/EmbedQuestIcons.py")
set(quest_icon_header "${CMAKE_CURRENT_BINARY_DIR}/generated/QuestIconPng.h")
add_custom_command(OUTPUT "${quest_icon_header}"
  COMMAND "${Python3_EXECUTABLE}" "${quest_icon_script}" "${quest_icon_atlas}" "${quest_icon_header}"
  DEPENDS "${quest_icon_script}" "${quest_icon_atlas}"
  VERBATIM)
target_sources(cubeworld PRIVATE "${quest_icon_header}")


set(alpha_world "${CMAKE_CURRENT_SOURCE_DIR}/src/cube/shaders")
set(alpha_ui "${CMAKE_CURRENT_SOURCE_DIR}/plasma/src/plasma/shaders")
add_vulkan_shader("${alpha_world}" WorldVertexShader vs_6_0 Alpha)
add_vulkan_shader("${alpha_world}" FixedVertexShader vs_6_0 Alpha)
foreach(name WorldPixelShader SkyPixelShader CloudPixelShader WaterPixelShader UnusedPixelShader FixedPixelShader)
  add_vulkan_shader("${alpha_world}" ${name} ps_6_0 Alpha)
endforeach()
add_custom_command(OUTPUT "${shader_output}/AlphaWaterPixelShader.h" APPEND
  DEPENDS "${release_root}/src/gfx/shaders/WaterOptics.hlsli")
foreach(name ShapeVertexShader SurfaceVertexShader)
  add_vulkan_shader("${alpha_ui}" ${name} vs_6_0 Alpha)
endforeach()
foreach(name ShapePixelShader SurfacePixelShader Downsample2PixelShader Downsample3PixelShader Downsample4PixelShader
             HorizontalBlurPixelShader VerticalBlurPixelShader)
  add_vulkan_shader("${alpha_ui}" ${name} ps_6_0 Alpha)
endforeach()
