# Conan's CMakeDeps targets are directory-scoped imported targets. Each
# sibling directory that calls find_package() must therefore load the upstream
# OpenCV targets in its own scope, while repeated calls in one directory should
# still be idempotent.
include_guard(DIRECTORY)

# OpenCV's installed export owns the dependency graph for its monolithic
# library. In static builds that graph includes bundled codec libraries and
# platform-specific acceleration archives/frameworks; flattening package files
# loses both dependency edges and stable link order.
if(NOT TARGET opencv_world)
    set(_baas_opencv_static_was_defined FALSE)
    if(DEFINED OpenCV_STATIC)
        set(_baas_opencv_static_was_defined TRUE)
        set(_baas_opencv_static_previous "${OpenCV_STATIC}")
    endif()
    set(OpenCV_STATIC @BAAS_OPENCV_STATIC@)

    # A Conan binary contains one build configuration, while multi-config
    # generators still validate every standard configuration at generate time.
    # Tell OpenCV to map those configurations to the one packaged binary.
    set(_baas_opencv_map_was_defined FALSE)
    if(DEFINED OPENCV_MAP_IMPORTED_CONFIG)
        set(_baas_opencv_map_was_defined TRUE)
        set(_baas_opencv_map_previous "${OPENCV_MAP_IMPORTED_CONFIG}")
    endif()
    set(OPENCV_MAP_IMPORTED_CONFIG
        "DEBUG=@BAAS_OPENCV_BUILD_TYPE@;RELEASE=@BAAS_OPENCV_BUILD_TYPE@;RELWITHDEBINFO=@BAAS_OPENCV_BUILD_TYPE@;MINSIZEREL=@BAAS_OPENCV_BUILD_TYPE@")

    set(_baas_opencv_config
        "${CMAKE_CURRENT_LIST_DIR}/../opencv4/OpenCVConfig.cmake")
    if(NOT EXISTS "${_baas_opencv_config}")
        message(FATAL_ERROR
            "baas-opencv package is missing its installed OpenCV target export")
    endif()
    include("${_baas_opencv_config}")

    if(_baas_opencv_static_was_defined)
        set(OpenCV_STATIC "${_baas_opencv_static_previous}")
    else()
        unset(OpenCV_STATIC)
    endif()
    if(_baas_opencv_map_was_defined)
        set(OPENCV_MAP_IMPORTED_CONFIG "${_baas_opencv_map_previous}")
    else()
        unset(OPENCV_MAP_IMPORTED_CONFIG)
    endif()
    unset(_baas_opencv_map_previous)
    unset(_baas_opencv_map_was_defined)
    unset(_baas_opencv_static_previous)
    unset(_baas_opencv_static_was_defined)
    unset(_baas_opencv_config)
endif()

if(NOT TARGET opencv_world)
    message(FATAL_ERROR
        "baas-opencv installed export did not define the opencv_world target")
endif()

target_link_libraries(BAAS::OpenCV INTERFACE opencv_world)
if(TARGET BAAS::OpenCVDnn)
    target_link_libraries(BAAS::OpenCVDnn INTERFACE opencv_world)
endif()
