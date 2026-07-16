cmake_minimum_required(VERSION 3.15)
project(baas_libgit2_test_package LANGUAGES CXX)

find_package(baas-libgit2 CONFIG REQUIRED)

add_executable(test_package test_package.cpp)
target_compile_features(test_package PRIVATE cxx_std_20)
target_link_libraries(test_package PRIVATE BAAS::libgit2)
