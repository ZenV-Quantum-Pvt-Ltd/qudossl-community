# Examples CMakeLists.txt

# Stale-API drift in these standalone-only sources must fail the build, not
# compile into sign-extended pointers (implicit decl returns int).
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror=implicit-function-declaration -Werror=int-conversion)
endif()

# Context and signature examples
add_executable(context_signature context_signature.c)
target_link_libraries(context_signature qudo-mldsa)

add_executable(speed_mldsa speed_mldsa.c)
target_link_libraries(speed_mldsa qudo-mldsa)

# 3-way benchmark comparison: OpenSSL native vs QUDO provider vs QUDO direct
# Requires OpenSSL 3.5.0+ with native ML-DSA support
if(OPENSSL_FOUND AND OPENSSL_VERSION VERSION_GREATER_EQUAL "3.5.0")
    add_executable(benchmark_comparison benchmark_comparison.c)
    target_link_libraries(benchmark_comparison qudo-mldsa OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "Building benchmark_comparison with OpenSSL ${OPENSSL_VERSION}")
else()
    message(STATUS "Skipping benchmark_comparison (OpenSSL 3.5.0+ required, found: ${OPENSSL_VERSION})")
endif()

# Advanced API examples - only in single-variant mode
if(NOT MLDSA_DIST_BUILD)
    add_executable(deterministic_keygen deterministic_keygen.c)
    target_link_libraries(deterministic_keygen qudo-mldsa)

    add_executable(deterministic_sign deterministic_sign.c)
    target_link_libraries(deterministic_sign qudo-mldsa)

    add_executable(external_mu external_mu.c)
    target_link_libraries(external_mu qudo-mldsa)

    add_executable(concat_format concat_format.c)
    target_link_libraries(concat_format qudo-mldsa)

    add_executable(prehash_sha256 prehash_sha256.c)
    target_link_libraries(prehash_sha256 qudo-mldsa)
endif()

# Install examples
set(MLDSA_INSTALL_TARGETS context_signature speed_mldsa)
if(TARGET benchmark_comparison)
    list(APPEND MLDSA_INSTALL_TARGETS benchmark_comparison)
endif()
if(NOT MLDSA_DIST_BUILD)
    list(APPEND MLDSA_INSTALL_TARGETS deterministic_keygen deterministic_sign external_mu concat_format prehash_sha256)
endif()
install(TARGETS ${MLDSA_INSTALL_TARGETS}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/qudo-mldsa-examples
)
