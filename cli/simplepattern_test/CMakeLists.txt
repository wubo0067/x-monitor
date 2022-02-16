AUX_SOURCE_DIRECTORY(. simplepattern_test_srcs)
add_executable(simplepattern_test ${simplepattern_test_srcs})

target_compile_definitions(simplepattern_test PRIVATE -DDEBUG)

target_link_libraries(simplepattern_test xmonitor-utils libzlog.a pthread z m)