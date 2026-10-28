cmake_minimum_required(VERSION 3.25...4.0)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

include(FeatureSummary)
include(CMakeDependentOption)

# We don't support in tree builds, so help people make the right choice.
if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

project(commando LANGUAGES C CXX)

include(CTest)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# This file handles extra settings wanted/needed for different compilers.
include(compilers)

# This file allows embedding resources
include(embed)

# Set where the build results will end up
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" CACHE PATH "Where to output archives")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" CACHE PATH "Where to output libraries")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" CACHE PATH "Where to output binaries")

option(W3D_CLIENT "Build full game client." ON)
add_feature_info(OpenW3DClient W3D_CLIENT "Build OpenW3D game client")

option(W3D_FDS "Build as headless server." ON)
add_feature_info(OpenW3DHeadless W3D_FDS "Build OpenW3D headless server")

# Do we want to build extra SDK stuff or just the game binary?
option(W3D_TOOLS "Build additional tools." ON)
add_feature_info(OpenW3DTools W3D_TOOLS "Build OpenW3D Mod Tools")

option(W3D_BUILD_DOCS "Enable the API documentation target." OFF)
add_feature_info(OpenW3DDocs W3D_BUILD_DOCS "Build OpenW3D API documentation")
if(W3D_BUILD_DOCS)
    add_subdirectory(docs)
endif()

# Do we want to embed git information into the binary?
option(W3D_EMBED_GIT_INFO "Embed information from git." ON)
add_feature_info(W3DEmbedGit W3D_EMBED_GIT_INFO "Embed information from git")

option(SYNTAX_CHECK_ONLY "Syntax check the files only?" OFF)
add_feature_info(SyntaxCheck SYNTAX_CHECK_ONLY "Syntax check files only")
if(SYNTAX_CHECK_ONLY)
    add_compile_options("-fsyntax-only")
endif(SYNTAX_CHECK_ONLY)

# Do we want to build with SDL3 for windows and inputs?
cmake_dependent_option(W3D_BUILD_OPTION_SDL3 "Build OpenW3D with SDL3." OFF WIN32 ON)
add_feature_info(SDL3Build W3D_BUILD_OPTION_SDL3 "Build OpenW3D with SDL3")

# Do we want to build with ffmpeg for audio/video decoding?
# Non-Windows builds always use FFmpeg. 64-bit MSVC builds default to it as well, because
# the only Miles and Bink binaries available are 32-bit and the stubs produce no sound or
# video. If the option was not set explicitly and FFmpeg or OpenAL cannot be found, those
# builds fall back to the stubs with a warning instead of failing.
if(WIN32)
    set(_w3d_media_default OFF)
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_w3d_media_default ON)
    endif()
    if(NOT DEFINED CACHE{W3D_BUILD_OPTION_FFMPEG})
        set(W3D_MEDIA_DEFAULTED ${_w3d_media_default} CACHE INTERNAL "FFmpeg/OpenAL were enabled by default rather than by the user")
    endif()
    option(W3D_BUILD_OPTION_FFMPEG "Build with ffmpeg." ${_w3d_media_default})

    if(W3D_BUILD_OPTION_FFMPEG AND W3D_MEDIA_DEFAULTED)
        find_package(FFmpeg QUIET COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE SWRESAMPLE)
        find_package(OpenAL QUIET)
        if(NOT FFmpeg_FOUND OR NOT OPENAL_FOUND)
            message(WARNING "FFmpeg or OpenAL not found; building with the silent Miles and Bink stubs. "
                "Use the win preset (vcpkg) or install them for sound and movies.")
            set(W3D_BUILD_OPTION_FFMPEG OFF)
        endif()
    endif()
else()
    set(W3D_BUILD_OPTION_FFMPEG ON)
endif()
add_feature_info(FFMpegBuild W3D_BUILD_OPTION_FFMPEG "Build OpenW3D with FFMpeg")

# Do we want to build with OpenAL/ffmpeg for audio playback?
cmake_dependent_option(W3D_BUILD_OPTION_OPENAL "Build with openal." ON W3D_BUILD_OPTION_FFMPEG OFF)
add_feature_info(OpenALBuild W3D_BUILD_OPTION_OPENAL "Build OpenW3D with OpenAL")

# Do we want to build with freetype for font rendering?
cmake_dependent_option(W3D_BUILD_OPTION_FREETYPE "Build with freetype." OFF WIN32 ON)
add_feature_info(FreetypeBuild W3D_BUILD_OPTION_FREETYPE "Build OpenW3D with freetype")

option(W3D_BUILD_QT_TOOLS "Build Qt-based GUI tools." OFF)
add_feature_info(QtTools W3D_BUILD_QT_TOOLS "Build Qt front-end tools")

if(NOT WIN32)
    add_compile_definitions(stricmp=strcasecmp)
    add_compile_definitions(strnicmp=strncasecmp)
endif()

include(FetchContent)

if(W3D_BUILD_OPTION_FFMPEG)
    include(ffmpeg)
endif()

if(W3D_BUILD_OPTION_SDL3)
    include(sdl3)
    add_compile_definitions(OPENW3D_SDL3=1)
elseif(WIN32)
    add_compile_definitions(OPENW3D_WIN32=1)
else()
    message(FATAL_ERROR "Invalid backend")
endif()

if(W3D_BUILD_OPTION_OPENAL)
    include(openal)
endif()

if(W3D_BUILD_OPTION_FREETYPE)
    include(freetype)
endif()

if(W3D_BUILD_QT_TOOLS)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Qt tooling is only supported for 64-bit builds. Configure with a 64-bit generator or set W3D_BUILD_QT_TOOLS=OFF.")
    endif()
    include(qt_tools)
endif()

if(WIN32)
    # Do we want to build with bink for video decoding?
    cmake_dependent_option(W3D_BUILD_OPTION_BINK "Build with bink." ON "NOT W3D_BUILD_OPTION_FFMPEG" OFF)
    add_feature_info(BinkBuild W3D_BUILD_OPTION_BINK "Build OpenW3D with Bink")

    if(W3D_BUILD_OPTION_BINK)
        include(bink)
        target_compile_definitions(binkstub INTERFACE W3D_HAS_BINK)
    endif()

    # Do we want to build with miles for audio playback?
    cmake_dependent_option(W3D_BUILD_OPTION_MILES "Build with miles." ON "NOT W3D_BUILD_OPTION_OPENAL" OFF)
    add_feature_info(MilesBuild W3D_BUILD_OPTION_MILES "Build OpenW3D with Miles Audio")

    if(W3D_BUILD_OPTION_MILES)
        include(miles)
    endif()

    option(W3D_BUILD_OPTION_ICU "Build with ICU." OFF)
    add_feature_info(ICUBuild W3D_BUILD_OPTION_ICU "Build OpenW3D with ICU")

    if(W3D_BUILD_OPTION_ICU)
        include(icu)
    endif()
else()
    set(W3D_BUILD_OPTION_ICU ON)
    include(icu)
endif()

include(dx9)

include(d3d12)


# Add main build targets
add_subdirectory(Code)

feature_summary(WHAT ENABLED_FEATURES DESCRIPTION "Enabled features:")
feature_summary(WHAT DISABLED_FEATURES DESCRIPTION "Disabled features:")
