cmake_minimum_required(VERSION 3.28)
project(sigcraft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-subobject-linkage")



set(IMR_FOLDER "/home/jackie/repos/imr2" CACHE STRING "Folder where IMR is found")
add_subdirectory(${IMR_FOLDER} imr_build)

if (NOT TARGET imr)
    message("IMR not found. Please set IMR_FOLDER accordingly.")
endif ()

add_subdirectory(enklume)

add_executable(sigcraft main.cpp camera.cpp chunk_mesh.cpp world.cpp)
target_link_libraries(sigcraft imr enklume nasl::nasl)

add_custom_target(dda_spv COMMAND ${GLSLANG_EXE} -V -S comp ${CMAKE_CURRENT_SOURCE_DIR}/shaders/dda.glsl -o ${CMAKE_CURRENT_BINARY_DIR}/dda.spv)
add_dependencies(sigcraft dda_spv)

