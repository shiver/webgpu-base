cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(PYTHON_EXECUTABLE "d:/apps/Python39/python.exe")

project(app)
add_executable(app "src/main.cpp")

set(DAWN_DEBUG_BREAK_ON_ERROR ON)
set_target_properties(app PROPERTIES
    CXX_STANDARD 20
    COMPILE_WARNING_AS_ERROR ON
)

if (MSVC)
    target_compile_options(app PRIVATE /W4)
else()
    target_compile_options(app PRIVATE -Wall -Wextra -pedantic)
endif()

if(EMSCRIPTEN)
  set_target_properties(app PROPERTIES SUFFIX ".html")
  target_link_options(app PRIVATE "-sUSE_WEBGPU=1" "-sUSE_GLFW=3")
else()
  set(DAWN_FETCH_DEPENDENCIES ON)
  add_subdirectory("dawn" EXCLUDE_FROM_ALL)

  if (APPLE)
      target_sources(dawn_glfw PRIVATE "src/metal.mm")
  endif()
  target_link_libraries(app PRIVATE webgpu_dawn webgpu_glfw)
endif()
