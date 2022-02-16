AUX_SOURCE_DIRECTORY(. mempool_test_srcs)
add_executable(mempool_test ${mempool_test_srcs})
target_link_libraries(mempool_test xmonitor-utils libzlog.a pthread z m)