AUX_SOURCE_DIRECTORY(. procfile_cli_srcs)
add_executable(procfile_cli ${procfile_cli_srcs})
target_link_libraries(procfile_cli xmonitor-utils libzlog.a pthread z m)