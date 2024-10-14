AUX_SOURCE_DIRECTORY(. xm_lambda_test_srcs)
add_executable(xm_lambda_test ${xm_lambda_test_srcs})

target_compile_definitions(xm_lambda_test PRIVATE -DDEBUG)