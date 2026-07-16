cmake_minimum_required(VERSION 3.22)
project(baas_miniz_test_package LANGUAGES CXX)

find_package(baas-miniz CONFIG REQUIRED)

if(NOT TARGET BAAS::miniz)
    message(FATAL_ERROR "baas-miniz did not export BAAS::miniz")
endif()

add_executable(baas_miniz_test test.cpp)
target_compile_features(baas_miniz_test PRIVATE cxx_std_20)
target_link_libraries(baas_miniz_test PRIVATE BAAS::miniz)
