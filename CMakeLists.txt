cmake_minimum_required(VERSION 3.13)
project(ffmpeg_example)

set(CMAKE_CXX_STANDARD 17)

add_subdirectory(video_encode)
add_subdirectory(video_decode)
add_subdirectory(video_hw_decode)