cmake_minimum_required(VERSION 3.20)
project(MapHappy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# Window + input
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# OpenGL function loader
FetchContent_Declare(glad
  GIT_REPOSITORY https://github.com/libigl/libigl-glad.git
  GIT_TAG 71e35fe685a0cc160068a2f2f971c40b82d14af0)

# UI library (has no CMake file of its own, so we build it below)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9)

FetchContent_MakeAvailable(glfw glad imgui)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw)

find_package(FFMPEG REQUIRED)

add_executable(MapHappy src/main.cpp)
target_include_directories(MapHappy PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(MapHappy PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(MapHappy PRIVATE glfw glad imgui_lib ${FFMPEG_LIBRARIES})