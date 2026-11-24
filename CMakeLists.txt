cmake_minimum_required(VERSION 3.20)
project(MapHappy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)       # some dependencies still declare an old minimum CMake version

option(MAPHAPPY_BUILD_TESTS "Build the automated tests (run them with ctest)" OFF)
option(MAPHAPPY_GL_TESTS    "Also build the tests that need an OpenGL context (run them under a virtual display, see README)" OFF)
set(MAPHAPPY_TEST_VIDEO "" CACHE FILEPATH "A short video file for the tests that need one (they skip themselves without it)")

include(FetchContent)

# ---- Window + input
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)    # X11 only (works under Wayland through XWayland); fewer build dependencies
endif()

# ---- OpenGL function loader
FetchContent_Declare(glad
  GIT_REPOSITORY https://github.com/libigl/libigl-glad.git
  GIT_TAG 71e35fe685a0cc160068a2f2f971c40b82d14af0)

# ---- UI library (has no CMake file of its own, so it is built below)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9)

# ---- Audio (a single header file) and JSON (header-only): downloaded but not built, we just use the headers
FetchContent_Declare(miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.22
  SOURCE_SUBDIR does_not_exist)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  SOURCE_SUBDIR does_not_exist)

FetchContent_MakeAvailable(glfw glad imgui miniaudio nlohmann_json)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw)

# ---- FFmpeg: through vcpkg (Windows, see README) or the system's package manager (Linux, macOS Homebrew)
find_package(FFMPEG QUIET)
if(NOT FFMPEG_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)
endif()
find_package(Threads REQUIRED)

# ---- Everything except the UI shell, as a library, so the app and the tests share the same compiled code
add_library(maphappy_core STATIC
  src/VideoDecoder.cpp
  src/Audio.cpp
  src/ProjectIO.cpp
  src/PlatformFiles.cpp
  src/TextRaster.cpp)
target_include_directories(maphappy_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FFMPEG_INCLUDE_DIRS}
  ${miniaudio_SOURCE_DIR}
  ${nlohmann_json_SOURCE_DIR}/include)
target_link_directories(maphappy_core PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(maphappy_core PUBLIC glfw glad imgui_lib ${FFMPEG_LIBRARIES} ${CMAKE_DL_LIBS} Threads::Threads)
if(APPLE)
  target_compile_definitions(maphappy_core PUBLIC GL_SILENCE_DEPRECATION)   # Apple deprecated OpenGL; it still works and we target 3.3 core
  # miniaudio's CoreAudio backend (it can load these at run time, but linking them explicitly always works)
  target_link_libraries(maphappy_core PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
if(MSVC)
  target_compile_options(maphappy_core PUBLIC /utf-8)
endif()

add_executable(MapHappy src/main.cpp)
target_link_libraries(MapHappy PRIVATE maphappy_core)

if(MAPHAPPY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
