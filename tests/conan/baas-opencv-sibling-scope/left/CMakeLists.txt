find_package(baas-opencv CONFIG REQUIRED)
find_package(baas-opencv CONFIG REQUIRED)

if(NOT TARGET opencv_world)
    message(FATAL_ERROR "left sibling cannot see the upstream OpenCV target")
endif()

add_executable(baas_opencv_scope_left ../main.cpp)
target_compile_features(baas_opencv_scope_left PRIVATE cxx_std_20)
target_link_libraries(baas_opencv_scope_left PRIVATE BAAS::OpenCV)
add_test(NAME baas_opencv_scope_left COMMAND baas_opencv_scope_left)
