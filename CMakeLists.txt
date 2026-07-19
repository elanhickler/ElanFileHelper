cmake_minimum_required(VERSION 3.15)
project(ElanFileHelper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ElanFileHelper STATIC
    include/FileHelper.cpp
    include/BatchFileHelper.cpp
)
target_include_directories(ElanFileHelper PUBLIC include)

if(WIN32)
    target_link_libraries(ElanFileHelper PUBLIC shell32)
endif()

add_executable(smoke_test tests/smoke_test.cpp)
target_link_libraries(smoke_test PRIVATE ElanFileHelper)
