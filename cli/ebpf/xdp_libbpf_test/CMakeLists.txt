# 目录下所有的源代码文件并将列表存储在cachestat_cli_srcs中
AUX_SOURCE_DIRECTORY(. xdp_libbpf_test_srcs)

include_directories("${PROJECT_SOURCE_DIR}/plugin_ebpf/usr"
                    "${EXTRA_DIR}/include/libbpf/libbpf/include"
                    "${EXTRA_DIR}/include/libbpf/libbpf/include/uapi")

add_executable(xdp_libbpf_test ${xdp_libbpf_test_srcs})
target_compile_definitions(xdp_libbpf_test PRIVATE -DDEBUG)

target_link_libraries(xdp_libbpf_test xmonitor-utils libzlog.a libbpf.a elf pthread z m)
