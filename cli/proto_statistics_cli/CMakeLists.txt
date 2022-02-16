# 目录下所有的源代码文件并将列表存储在proto_statistics_cli_srcs中
AUX_SOURCE_DIRECTORY(. proto_statistics_cli_srcs)

set(EBPF_HELPER_OBJS
	${KERNEL_DIR}/tools/testing/selftests/bpf/trace_helpers.o)

include_directories("${EBPF_USER_INCLUDE_DIRS}")

link_directories("${KERNEL_DIR}/tools/lib/bpf")

# 加上trace_helpers.o文件到链接队列中
add_executable(proto_statistics_cli ${proto_statistics_cli_srcs} ${EBPF_HELPER_OBJS})
target_compile_definitions(proto_statistics_cli PRIVATE -DDEBUG)

add_custom_target(gen_proto_statistics_skel 
				COMMAND /usr/sbin/bpftool gen skeleton ${PROJECT_SOURCE_DIR}/collectors/ebpf/kernel/xmbpf_proto_statistics_kern.o > ${PROJECT_SOURCE_DIR}/cli/proto_statistics_cli/xmbpf_proto_statistics_skel.h
				VERBATIM
				)

target_link_libraries(proto_statistics_cli xmonitor-utils libbpf.a elf pthread z m) 
add_dependencies(proto_statistics_cli gen_proto_statistics_skel)   