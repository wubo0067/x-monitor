AUX_SOURCE_DIRECTORY(. regex_cli_srcs)
add_executable(regex_cli ${regex_cli_srcs})
target_link_libraries(regex_cli xmonitor-utils  libpcre2-8.a libzlog.a pthread z m)