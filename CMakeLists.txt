cmake_minimum_required(VERSION 3.25)
project(CloudPlay VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cloudplay_options INTERFACE)
if(MSVC)
  target_compile_options(cloudplay_options INTERFACE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(cloudplay_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

add_subdirectory(host)
