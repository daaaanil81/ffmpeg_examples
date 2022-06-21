cmake_minimum_required(VERSION 3.18)

project(FFmpegExamples)

set(CMAKE_CXX_STANDARD 11)
set(USR_LOCAL_LIB_DIR "/usr/local/lib")
set(USR_LOCAL_INCLUDE_DIR "/usr/local/include")

find_package(OpenCV REQUIRED)

add_executable(detail_video_info detail_video_info.cpp)

target_include_directories(detail_video_info PRIVATE
    ${USR_LOCAL_INCLUDE_DIR}
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(detail_video_info
    ${USR_LOCAL_LIB_DIR}/libavcodec.so
    ${USR_LOCAL_LIB_DIR}/libavformat.so
    ${USR_LOCAL_LIB_DIR}/libavutil.so
    ${USR_LOCAL_LIB_DIR}/libswscale.so
    ${OpenCV_LIBRARIES}
)

