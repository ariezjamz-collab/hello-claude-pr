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

# The SDK is a single unity-build translation unit, compiled into the plugin itself.
add_library(ImagFX MODULE
    src/ImagFX.cpp
    src/ImagFX.h
    src/ImagFXShaders.h
    ${FFGL_SDK_DIR}/source/lib/FFGLSDK.cpp)
target_include_directories(ImagFX PRIVATE ${FFGL_SDK_DIR}/source/lib)

if(WIN32)
    # Link GLEW statically from the copy bundled with the SDK, so there is no DLL to ship.
    target_include_directories(ImagFX PRIVATE ${FFGL_SDK_DIR}/deps/glew-2.1.0/include)
    target_compile_definitions(ImagFX PRIVATE GLEW_STATIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(ImagFX PRIVATE
        ${FFGL_SDK_DIR}/deps/glew-2.1.0/lib/Release/x64/glew32s.lib
        opengl32)
    set_target_properties(ImagFX PROPERTIES PREFIX "" SUFFIX ".dll")
elseif(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(ImagFX PRIVATE ${OPENGL_FRAMEWORK})
    target_compile_definitions(ImagFX PRIVATE GL_SILENCE_DEPRECATION)
    set_target_properties(ImagFX PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION bundle
        OSX_ARCHITECTURES "arm64;x86_64")  # Arena runs natively on Apple Silicon
else()
    # Resolume doesn't run on Linux; this target exists so the plugin can be built and tested headless.
    find_package(GLEW REQUIRED)
    find_package(OpenGL REQUIRED)
    target_link_libraries(ImagFX PRIVATE GLEW::GLEW OpenGL::GL)
    set_target_properties(ImagFX PROPERTIES PREFIX "")
endif()

option(IMAGFX_BUILD_TEST_HOST "Build the headless test host (Linux, EGL)" OFF)
if(IMAGFX_BUILD_TEST_HOST)
    add_subdirectory(test)
endif()
