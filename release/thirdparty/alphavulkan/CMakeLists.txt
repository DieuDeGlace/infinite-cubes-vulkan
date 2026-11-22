include(FetchContent)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR none
)
FetchContent_MakeAvailable(glm)

add_library(alphavulkan STATIC
  alphagen/Landscape.cpp
  alphagen/LandscapeStore.cpp
  alphagen/Locations.cpp
  alphagen/Random.cpp
  alphagen/Regions.cpp
  alphagen/Relief.cpp
  alphagen/SurfaceStore.cpp
  alphagen/Terrain.cpp
  alphagen/TreeModels.cpp
  alphagen/Trees.cpp
  alphagen/Vegetation.cpp
  alphagen/Zone.cpp
)
target_include_directories(alphavulkan PUBLIC .)
target_include_directories(alphavulkan SYSTEM PUBLIC "${glm_SOURCE_DIR}")
if(MSVC)
  target_compile_options(alphavulkan PRIVATE /fp:strict)
else()
  target_compile_options(alphavulkan PRIVATE -ffp-contract=off)
endif()
