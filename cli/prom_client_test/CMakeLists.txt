AUX_SOURCE_DIRECTORY(. prom_client_test_srcs)
add_executable(prom_client_test ${prom_client_test_srcs})
target_link_libraries(prom_client_test xmonitor-utils ${EXTRA_LIBS} pthread z m)