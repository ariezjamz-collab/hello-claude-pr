cmake_minimum_required(VERSION 3.15)
project(ImagFX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Resolume's FFGL SDK. Point FFGL_SDK_DIR at an existing checkout, or let CMake fetch it.
set(FFGL_SDK_DIR "" CACHE PATH "Path to a checkout of https://github.com/resolume/ffgl")
if(NOT FFGL_SDK_DIR)
    include(FetchContent)
    FetchContent_Declare(ffgl
        GIT_REPOSITORY https://github.com/resolume/ffgl.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(ffgl)
    if(NOT ffgl_POPULATED)
        FetchContent_Populate(ffgl)  # sources only; we don't use the SDK's own CMake targets
    endif()
    set(FFGL_SDK_DIR ${ffgl_SOURCE_DIR})
endif()

# Each plugin is its own binary (the FFGL SDK supports one plugin per file). The SDK is a single
# unity-build translation unit, compiled into each plugin.
function(add_imag_plugin target)
    add_library(${target} MODULE ${ARGN} ${FFGL_SDK_DIR}/source/lib/FFGLSDK.cpp)
    target_include_directories(${target} PRIVATE ${FFGL_SDK_DIR}/source/lib)

    if(WIN32)
        # Link GLEW statically from the copy bundled with the SDK, so there is no DLL to ship.
        target_include_directories(${target} PRIVATE ${FFGL_SDK_DIR}/deps/glew-2.1.0/include)
        target_compile_definitions(${target} PRIVATE GLEW_STATIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(${target} PRIVATE
            ${FFGL_SDK_DIR}/deps/glew-2.1.0/lib/Release/x64/glew32s.lib
            opengl32)
        set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".dll")
    elseif(APPLE)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        target_link_libraries(${target} PRIVATE ${OPENGL_FRAMEWORK})
        target_compile_definitions(${target} PRIVATE GL_SILENCE_DEPRECATION)
        set_target_properties(${target} PROPERTIES
            BUNDLE TRUE
            BUNDLE_EXTENSION bundle
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER com.imagfx.${target}
            MACOSX_BUNDLE_BUNDLE_VERSION 0.2.0
            MACOSX_BUNDLE_SHORT_VERSION_STRING 0.2.0
            OSX_ARCHITECTURES "arm64;x86_64"  # Arena runs natively on Apple Silicon
            OSX_DEPLOYMENT_TARGET 10.13)
    else()
        # Resolume doesn't run on Linux; these targets exist so the plugins can be built and tested headless.
        find_package(GLEW REQUIRED)
        find_package(OpenGL REQUIRED)
        target_link_libraries(${target} PRIVATE GLEW::GLEW OpenGL::GL)
        set_target_properties(${target} PROPERTIES PREFIX "")
    endif()
endfunction()

# libfacedetection (BSD 3-Clause, https://github.com/ShiqiYu/libfacedetection) for IMAG Face.
set(FACEDETECT_DIR "" CACHE PATH "Path to a checkout of https://github.com/ShiqiYu/libfacedetection")
if(NOT FACEDETECT_DIR)
    include(FetchContent)
    FetchContent_Declare(facedetection
        URL https://github.com/ShiqiYu/libfacedetection/archive/acf7b254121927e7dced30e233a2e03119f28ea2.tar.gz)
    FetchContent_GetProperties(facedetection)
    if(NOT facedetection_POPULATED)
        FetchContent_Populate(facedetection)
    endif()
    set(FACEDETECT_DIR ${facedetection_SOURCE_DIR})
endif()
set(FACEDETECT_SOURCES
    ${FACEDETECT_DIR}/src/facedetectcnn.cpp
    ${FACEDETECT_DIR}/src/facedetectcnn-model.cpp
    ${FACEDETECT_DIR}/src/facedetectcnn-data.cpp)
# Only the detector's own files get the SIMD flags; the plugin checks for AVX2 at runtime before using it.
set_source_files_properties(${FACEDETECT_SOURCES} PROPERTIES
    INCLUDE_DIRECTORIES "${FACEDETECT_DIR}/src;${CMAKE_CURRENT_SOURCE_DIR}/third_party/facedetection")
if(MSVC)
    set_source_files_properties(${FACEDETECT_SOURCES} PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/W0")
elseif(APPLE)
    # Universal binary: AVX2 for the Intel slice; the Apple Silicon slice uses NEON (see the export shim).
    set_source_files_properties(${FACEDETECT_SOURCES} PROPERTIES COMPILE_OPTIONS
        "SHELL:-Xarch_x86_64 -mavx2;SHELL:-Xarch_x86_64 -mfma;-w")
else()
    set_source_files_properties(${FACEDETECT_SOURCES} PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-w")
endif()

add_imag_plugin(ImagFX src/ImagFX.cpp src/ImagFX.h src/PassThrough.h src/ImagFXShaders.h src/ShaderCommon.h src/PluginLog.h)
add_imag_plugin(ImagFlame src/ImagFlame.cpp src/ImagFlame.h src/PassThrough.h src/ImagFlameShaders.h src/ShaderCommon.h src/PluginLog.h)
add_imag_plugin(ImagBlob src/ImagBlob.cpp src/ImagBlob.h src/PassThrough.h src/TrackOverlay.h src/TrackOverlayShaders.h src/ColorUtil.h src/ShaderCommon.h src/PluginLog.h)
add_imag_plugin(ImagFace src/ImagFace.cpp src/ImagFace.h src/FaceDetector.h src/TrackOverlay.h src/TrackOverlayShaders.h src/ColorUtil.h src/ShaderCommon.h src/PluginLog.h ${FACEDETECT_SOURCES})
find_package(Threads REQUIRED)
target_link_libraries(ImagFace PRIVATE Threads::Threads)
add_imag_plugin(ImagKey src/ImagKey.cpp src/ImagKey.h src/PassThrough.h src/ImagKeyShaders.h src/ShaderCommon.h src/PluginLog.h)
add_imag_plugin(ImagLooks src/ImagLooks.cpp src/ImagLooks.h src/PassThrough.h src/ImagLooksShaders.h src/ShaderCommon.h src/PluginLog.h)

option(IMAGFX_BUILD_TEST_HOST "Build the headless test host (Linux, EGL)" OFF)
if(IMAGFX_BUILD_TEST_HOST)
    add_subdirectory(test)
endif()
