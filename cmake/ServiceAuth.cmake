include_guard(GLOBAL)

# The dependency integration owns provisioning. A direct CONFIG lookup keeps
# this standalone target usable with a Conan CMakeDeps generator while still
# requiring the single canonical imported target.
if(NOT TARGET BAAS::sodium)
    find_package(baas-libsodium CONFIG QUIET)
endif()
if(NOT TARGET BAAS::sodium)
    message(FATAL_ERROR "BAAS service auth crypto requires imported target BAAS::sodium")
endif()

add_library(
        BAAS_service_auth_crypto
        STATIC
        "${BAAS_PROJECT_PATH}/src/service/auth/CanonicalJson.cpp"
        "${BAAS_PROJECT_PATH}/src/service/auth/Crypto.cpp"
        "${BAAS_PROJECT_PATH}/src/service/auth/SecureEnvelope.cpp"
)
target_compile_features(BAAS_service_auth_crypto PUBLIC cxx_std_20)
target_include_directories(
        BAAS_service_auth_crypto
        PUBLIC
        "${BAAS_PROJECT_PATH}/include"
)
target_link_libraries(BAAS_service_auth_crypto PUBLIC BAAS::sodium)

if(MSVC)
    target_compile_options(BAAS_service_auth_crypto PUBLIC /utf-8)
    target_compile_options(BAAS_service_auth_crypto PRIVATE /W4 /permissive-)
else()
    target_compile_options(BAAS_service_auth_crypto PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_SERVICE_AUTH_OWNER)
    add_library(
            BAAS_service_auth_owner
            STATIC
            "${BAAS_PROJECT_PATH}/src/service/auth/AuthOwner.cpp"
    )
    target_compile_features(BAAS_service_auth_owner PUBLIC cxx_std_20)
    target_include_directories(BAAS_service_auth_owner PUBLIC "${BAAS_PROJECT_PATH}/include")
    target_link_libraries(BAAS_service_auth_owner PUBLIC BAAS_service_auth_crypto)
    if(WIN32)
        target_link_libraries(BAAS_service_auth_owner PRIVATE advapi32)
    endif()
    if(MSVC)
        target_compile_options(BAAS_service_auth_owner PUBLIC /utf-8)
        target_compile_options(BAAS_service_auth_owner PRIVATE /W4 /permissive-)
    else()
        target_compile_options(BAAS_service_auth_owner PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BUILD_SERVICE_AUTH_CRYPTO_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_auth_crypto_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceAuthCryptoTests.cpp"
    )
    target_compile_features(BAAS_service_auth_crypto_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_service_auth_crypto_tests
            PRIVATE
            BAAS_SERVICE_CONTRACT_VECTOR_PATH="${BAAS_PROJECT_PATH}/tests/service_contract/v1_vectors.json"
    )
    target_link_libraries(
            BAAS_service_auth_crypto_tests
            PRIVATE BAAS_service_auth_crypto
    )
    add_test(
            NAME BAAS_service_auth_crypto_tests
            COMMAND BAAS_service_auth_crypto_tests
    )
    set_tests_properties(
            BAAS_service_auth_crypto_tests
            PROPERTIES TIMEOUT 60
    )
endif()

if(BUILD_SERVICE_AUTH_OWNER_TESTS)
    include(CTest)
    add_executable(
            BAAS_service_auth_owner_tests
            "${BAAS_PROJECT_PATH}/tests/service/ServiceAuthOwnerTests.cpp"
    )
    target_compile_features(BAAS_service_auth_owner_tests PRIVATE cxx_std_20)
    target_compile_definitions(
            BAAS_service_auth_owner_tests
            PRIVATE
            BAAS_SERVICE_CONTRACT_VECTOR_PATH="${BAAS_PROJECT_PATH}/tests/service_contract/v1_vectors.json"
    )
    target_link_libraries(BAAS_service_auth_owner_tests PRIVATE BAAS_service_auth_owner)
    add_test(NAME BAAS_service_auth_owner_tests COMMAND BAAS_service_auth_owner_tests)
    set_tests_properties(BAAS_service_auth_owner_tests PROPERTIES TIMEOUT 60)
endif()
