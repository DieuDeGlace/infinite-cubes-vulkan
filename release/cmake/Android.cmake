# The Android build of the full game: the shared game sources with Android's window, input, audio and image loading.
include(FetchContent)

# Match the x64 Windows build where the C++ standard leaves a choice: plain char is signed there, and its compilers
# do not fuse a multiplication and an addition into one rounding. World generation depends on both.
add_compile_options(-fsigned-char -ffp-contract=off)

function(add_android_library name)
  add_library(${name} STATIC ${ARGN})
  set_target_properties(${name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_options(${name} PRIVATE -w)
endfunction()

execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExpandSQLite.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sqlite/sqlite3.c.xz" "${CMAKE_CURRENT_BINARY_DIR}/sqlite3.c"
  COMMAND_ERROR_IS_FATAL ANY)
add_android_library(sqlite "${CMAKE_CURRENT_BINARY_DIR}/sqlite3.c")
target_include_directories(sqlite PUBLIC thirdparty/sqlite)
# Bionic's fortified umask is overloaded, and SQLite stores the function's address in its system call table.
target_compile_options(sqlite PRIVATE -U_FORTIFY_SOURCE)

add_android_library(libogg thirdparty/libogg/src/framing.c thirdparty/libogg/src/bitwise.c)
target_include_directories(libogg PUBLIC thirdparty/libogg/include platform/android/compat)

set(vorbis_sources analysis bitrate block codebook envelope floor0 floor1 info lookup lpc lsp mapping0 mdct psy
  registry res0 sharedbook smallft synthesis vorbisenc window vorbisfile)
list(TRANSFORM vorbis_sources PREPEND thirdparty/libvorbis/lib/)
list(TRANSFORM vorbis_sources APPEND .c)
add_android_library(libvorbis ${vorbis_sources})
target_include_directories(libvorbis PUBLIC thirdparty/libvorbis/include)
target_include_directories(libvorbis PRIVATE thirdparty/libvorbis/lib)
target_link_libraries(libvorbis PUBLIC libogg)

# The x64 assembly inflate of the Windows build is left out.
set(zlib_sources adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees
  uncompr zutil contrib/minizip/ioapi contrib/minizip/unzip contrib/minizip/zip)
list(TRANSFORM zlib_sources PREPEND thirdparty/zlib/)
list(TRANSFORM zlib_sources APPEND .c)
add_android_library(zlib ${zlib_sources})
target_include_directories(zlib PUBLIC thirdparty/zlib)
target_compile_definitions(zlib PUBLIC Z_PREFIX Z_HAVE_UNISTD_H)
target_compile_definitions(zlib PRIVATE z_errmsg=cube_z_errmsg)

set(freetype_sources autofit/autofit bdf/bdf cff/cff base/ftbase base/ftbitmap cache/ftcache base/ftdebug
  base/ftfstype base/ftgasp base/ftglyph gzip/ftgzip base/ftinit lzw/ftlzw base/ftstroke base/ftsystem smooth/smooth
  base/ftbbox base/ftmm base/ftpfr base/ftsynth base/fttype1 base/ftwinfnt base/ftxf86 base/ftlcdfil base/ftgxval
  base/ftotval base/ftpatent pcf/pcf pfr/pfr psaux/psaux pshinter/pshinter psnames/psmodule raster/raster sfnt/sfnt
  truetype/truetype type1/type1 cid/type1cid type42/type42 winfonts/winfnt)
list(TRANSFORM freetype_sources PREPEND thirdparty/freetype/src/)
list(TRANSFORM freetype_sources APPEND .c)
add_android_library(freetype ${freetype_sources})
target_include_directories(freetype PUBLIC thirdparty/freetype/include)
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)

add_android_library(pugixml thirdparty/pugixml/src/pugixml.cpp)
target_include_directories(pugixml PUBLIC thirdparty/pugixml/src)
target_compile_definitions(pugixml PUBLIC PUGIXML_WCHAR_MODE)

# Plasma triangulates its vector shapes with the GLU tessellator, which Windows provides in glu32.
FetchContent_Declare(glu
  GIT_REPOSITORY https://gitlab.freedesktop.org/mesa/glu.git
  GIT_TAG glu-9.0.3 GIT_SHALLOW TRUE SOURCE_SUBDIR unused)
# stb_image decodes the BMP, PNG, JPEG and TGA files that FreeImage decodes on Windows.
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 9f755b604db3492df2cbeb39bf84b81e3e3932e3 SOURCE_SUBDIR unused)
FetchContent_MakeAvailable(glu stb)
set(tess_sources dict geom memalloc mesh normal priorityq render sweep tess tessmono)
list(TRANSFORM tess_sources PREPEND "${glu_SOURCE_DIR}/src/libtess/")
list(TRANSFORM tess_sources APPEND .c)
add_android_library(glutess ${tess_sources})
target_include_directories(glutess PUBLIC platform/android/compat "${glu_SOURCE_DIR}/include")
target_include_directories(glutess PRIVATE "${glu_SOURCE_DIR}/src/include")

add_subdirectory(thirdparty/alphavulkan)
set_target_properties(alphavulkan PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS src/*.cpp plasma/src/*.cpp)
# The Windows entry point and XAudio2 have Android counterparts under platform/android.
list(FILTER GAME_SOURCES EXCLUDE REGEX "/src/(main|cube/XAudio2Engine)\\.cpp$")
file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS platform/android/*.cpp)
set(native_glue "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
add_library(cubeworld SHARED ${GAME_SOURCES} ${ANDROID_SOURCES} platform/SteamOffline.cpp
  "${native_glue}/android_native_app_glue.c")
# The compatibility headers stand in for the Windows SDK headers the shared sources include.
target_include_directories(cubeworld BEFORE PRIVATE platform/android/compat)
target_include_directories(cubeworld PRIVATE include plasma/include platform/android thirdparty/steamworks
  "${native_glue}" "${stb_SOURCE_DIR}")
# Microsoft spellings in the shared sources: the 64-bit integer keyword and the C runtime's daylight-saving flag.
target_compile_definitions(cubeworld PRIVATE STEAM_API_NODLL CUBE_OFFLINE "__int64=long long" _daylight=daylight)
target_compile_options(cubeworld PRIVATE -Wno-multichar
  "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/platform/android/compat/MsvcRandom.h>")
target_link_libraries(cubeworld PRIVATE alphavulkan pugixml sqlite libvorbis libogg zlib freetype glutess
  android log aaudio)
# android_main is reached through the glue's ANativeActivity_onCreate, which nothing in the library references.
target_link_options(cubeworld PRIVATE -u ANativeActivity_onCreate)

include(cmake/Vulkan.cmake)

set(quest_icon_atlas "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/overworldevents/quest-icons.png")
set(quest_icon_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedQuestIcons.py")
set(quest_icon_header "${CMAKE_CURRENT_BINARY_DIR}/generated/QuestIconPng.h")
add_custom_command(OUTPUT "${quest_icon_header}"
  COMMAND "${Python3_EXECUTABLE}" "${quest_icon_script}" "${quest_icon_atlas}" "${quest_icon_header}"
  DEPENDS "${quest_icon_script}" "${quest_icon_atlas}"
  VERBATIM)
target_sources(cubeworld PRIVATE "${quest_icon_header}")
