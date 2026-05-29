cmake_minimum_required(VERSION 3.10)
project(Grape VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TSM REQUIRED libtsm)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(GL REQUIRED gl)

include_directories(include)
include_directories(${TSM_INCLUDE_DIRS})
include_directories(${GLFW_INCLUDE_DIRS})
include_directories(${GL_INCLUDE_DIRS})

add_executable(demo examples/demo.cpp)
target_link_libraries(demo ${TSM_LIBRARIES} ${GLFW_LIBRARIES} ${GL_LIBRARIES})
