cmake_minimum_required(VERSION 3.23)
project(baas_opencv_sibling_scope_smoke LANGUAGES CXX)

include(CTest)
add_subdirectory(left)
add_subdirectory(right)
