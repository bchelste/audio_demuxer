cmake_minimum_required(VERSION 3.12)
project(audio_transcoder)
set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES audio_demuxer.cpp audio_resampler.cpp main.cpp)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
include_directories(SYSTEM ${CONAN_INCLUDE_DIRS})
SET(CONAN_DISABLE_CHECK_COMPILER 1)
conan_basic_setup(NO_OUTPUT_DIRS TARGETS)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        CONAN_PKG::ffmpeg
)