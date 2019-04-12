cmake_minimum_required (VERSION 3.11.0)
project (Morph)
# NB: compile with debug symbols
set(CMAKE_CXX_FLAGS "-std=c++0x -g") 

# Convenience variables
# CDIR is the directory of the CMakeLists.txt file
set(CDIR ${CMAKE_CURRENT_SOURCE_DIR})
# BDIR is the build/ directory
set(BDIR ${CMAKE_CURRENT_BINARY_DIR})

find_package(glfw3 3.2 REQUIRED)

# compile ImGui to a static lib
set(IMGUI "${CDIR}/third_party/imgui")
file(GLOB IMGUI_TOPLEVEL_SOURCES "${IMGUI}/*.cpp")
list(APPEND IMGUI_SOURCES
  ${IMGUI_TOPLEVEL_SOURCES}
  "${IMGUI}/examples/imgui_impl_glfw.cpp"
  "${IMGUI}/examples/imgui_impl_opengl3.cpp")
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${IMGUI} "${IMGUI}/examples" include)
target_compile_definitions(imgui PUBLIC
  IMGUI_IMPL_OPENGL_LOADER_GLAD=1)

# compile the app

set(DRIVER "${CDIR}/src/main.cpp")
file(GLOB SOURCES "src/*.cpp" "src/*.c")
list(REMOVE_ITEM SOURCES ${DRIVER})
add_library(main_lib STATIC ${SOURCES})
target_include_directories(main_lib PUBLIC include)
target_link_libraries(main_lib PUBLIC glfw imgui)

add_executable(main_exec ${DRIVER})
target_link_libraries(main_exec PUBLIC main_lib)

