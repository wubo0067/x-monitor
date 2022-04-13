AUX_SOURCE_DIRECTORY(. process_stat_test_srcs)
add_executable(process_stat_test ${process_stat_test_srcs})
target_link_libraries(process_stat_test xmonitor-process-stat xmonitor-utils libzlog.a libpagemap.a pthread z m)