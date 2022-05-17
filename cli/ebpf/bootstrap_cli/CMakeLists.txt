AUX_SOURCE_DIRECTORY(. bootstrap_cli_srcs)

include_directories("${PROJECT_SOURCE_DIR}/collectors/ebpf/user"
                    "${EXTRA_DIR}/include/libbpf/libbpf/include"
                    "${EXTRA_DIR}/include/libbpf/libbpf/include/uapi")

add_executable(bootstrap_cli ${bootstrap_cli_srcs})
#target_compile_definitions(bootstrap_cli PRIVATE -DDEBUG)

target_link_libraries(bootstrap_cli xmonitor-utils libzlog.a libbpf.a libcollectc.a elf pthread z m)