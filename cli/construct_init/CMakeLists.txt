AUX_SOURCE_DIRECTORY(. construct_init_srcs)
add_executable(construct_init ${construct_init_srcs})
target_link_libraries(construct_init xmonitor-utils pthread z m)