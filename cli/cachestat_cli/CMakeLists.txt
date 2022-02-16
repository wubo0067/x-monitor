# 目录下所有的源代码文件并将列表存储在cachestat_cli_srcs中
AUX_SOURCE_DIRECTORY(. cachestat_cli_srcs)

set(EBPF_HELPER_OBJS
	${KERNEL_DIR}/tools/testing/selftests/bpf/trace_helpers.o)

include_directories("${EBPF_USER_INCLUDE_DIRS}")

link_directories("${KERNEL_DIR}/tools/lib/bpf")

# 加上trace_helpers.o文件到链接队列中
add_executable(cachestat_cli ${cachestat_cli_srcs} ${EBPF_HELPER_OBJS})

add_custom_target(gen_cachestat_skel 
				COMMAND /usr/sbin/bpftool gen skeleton ${PROJECT_SOURCE_DIR}/collectors/ebpf/kernel/xmbpf_cachestat_kern.o > ${PROJECT_SOURCE_DIR}/cli/cachestat_cli/xmbpf_cachestat_skel.h
				VERBATIM
				)

target_link_libraries(cachestat_cli xmonitor-utils libzlog.a libbpf.a elf pthread z m) 
add_dependencies(cachestat_cli gen_cachestat_skel)               