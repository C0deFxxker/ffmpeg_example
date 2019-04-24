cmake_minimum_required(VERSION 3.13)
project(video_hw_decode)

set(CMAKE_CXX_STANDARD 17)

include(FindPkgConfig)
pkg_check_modules(FFMPEG REQUIRED ffmpeg-4.1.1)

include_directories(${FFMPEG_INCLUDE_DIRS})
link_directories(${FFMPEG_LIBRARY_DIRS})
link_libraries(${FFMPEG_LINK_LIBRARIES})

add_executable(${PROJECT_NAME} video_hw_decode.cpp)

target_compile_options(${PROJECT_NAME} PUBLIC ${FFMPEG_CFLAGS_OTHER})