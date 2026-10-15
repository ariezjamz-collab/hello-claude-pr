find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS OpenGL GLX)
find_package(X11 REQUIRED)

add_executable(headless_host headless_host.cpp)
target_include_directories(headless_host PRIVATE ${FFGL_SDK_DIR}/source/lib)
target_link_libraries(headless_host PRIVATE GLEW::GLEW OpenGL::GL OpenGL::GLX X11::X11 ${CMAKE_DL_LIBS})
