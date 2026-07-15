cmake_minimum_required(VERSION 3.22)
project(baas_libsodium_test_package LANGUAGES CXX)

find_package(baas-libsodium CONFIG REQUIRED)

if(NOT TARGET BAAS::sodium)
    message(FATAL_ERROR "baas-libsodium did not export BAAS::sodium")
endif()

add_executable(baas_libsodium_test test.cpp)
target_compile_features(baas_libsodium_test PRIVATE cxx_std_20)
target_link_libraries(baas_libsodium_test PRIVATE BAAS::sodium)
