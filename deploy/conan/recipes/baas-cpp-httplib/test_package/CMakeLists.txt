cmake_minimum_required(VERSION 3.22)
project(baas_cpp_httplib_test_package LANGUAGES CXX)

find_package(baas-cpp-httplib CONFIG REQUIRED)

add_executable(baas_cpp_httplib_test test.cpp)
target_compile_features(baas_cpp_httplib_test PRIVATE cxx_std_20)
target_link_libraries(baas_cpp_httplib_test PRIVATE BAAS::httplib)
