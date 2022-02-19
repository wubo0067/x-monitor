AUX_SOURCE_DIRECTORY(. cachestat_cli_srcs)

include_directories("${PROJECT_SOURCE_DIR}/collectors/ebpf/user"
                    "${EXTRA_DIR}/include/libbpf/libbpf/include"
                    "${EXTRA_DIR}/include/libbpf/libbpf/include/uapi")

add_executable(cachestat_cli ${cachestat_cli_srcs})
target_compile_definitions(cachestat_cli PRIVATE -DDEBUG)

target_link_libraries(cachestat_cli xmonitor-utils libzlog.a libbpf.a elf pthread z m)