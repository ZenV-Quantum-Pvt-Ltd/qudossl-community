# Examples CMakeLists.txt

# Stale-API drift in these standalone-only sources must fail the build, not
# compile into sign-extended pointers (implicit decl returns int).
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Werror=implicit-function-declaration -Werror=int-conversion)
endif()

add_executable(simple_sign simple_sign.c)
target_link_libraries(simple_sign qudo-slhdsa)

add_executable(speed_slhdsa speed_slhdsa.c)
target_link_libraries(speed_slhdsa qudo-slhdsa)

add_executable(der_pem_example der_pem_example.c)
target_link_libraries(der_pem_example qudo-slhdsa)
