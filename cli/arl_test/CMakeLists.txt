AUX_SOURCE_DIRECTORY(. arl_test_srcs)
add_executable(arl_test ${arl_test_srcs})
target_link_libraries(arl_test xmonitor-utils libzlog.a pthread z m)