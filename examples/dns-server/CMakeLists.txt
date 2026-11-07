cmake_minimum_required(VERSION 3.16)
project(steward_dns LANGUAGES CXX)

add_executable(steward-dns src/main.cpp)
target_compile_features(steward-dns PRIVATE cxx_std_17)
if(WIN32)
    target_link_libraries(steward-dns PRIVATE ws2_32)
endif()
