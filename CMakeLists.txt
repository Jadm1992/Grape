cmake_minimum_required(VERSION 3.14)
project(Grape VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch GLFW automatically so Windows users don't need it installed
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)

include_directories(include)
include_directories(vendor/libtsm/src/tsm)
include_directories(vendor/libtsm/src/shared)
include_directories(vendor/libtsm/external)
include_directories(vendor/libtsm/external/wcwidth)

add_executable(demo 
    examples/demo.cpp
    vendor/libtsm/src/tsm/tsm-render.c
    vendor/libtsm/src/tsm/tsm-screen.c
    vendor/libtsm/src/tsm/tsm-selection.c
    vendor/libtsm/src/tsm/tsm-unicode.c
    vendor/libtsm/src/tsm/tsm-vte-charsets.c
    vendor/libtsm/src/tsm/tsm-vte.c
    vendor/libtsm/src/shared/shl-htable.c
    vendor/libtsm/external/wcwidth/wcwidth.c
)

target_compile_definitions(demo PRIVATE BUILD_HAVE_WCWIDTH=1)

target_link_libraries(demo glfw OpenGL::GL)
