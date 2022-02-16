bin/perf_event_stack_cli --kern=../collectors/ebpf/kernel/xmbpf_perf_event_stack_kern.5.12.o --pids=310378 --duration=10 --output=2

make perf_event_stack_cli VERBOSE=1

cmake3 ../ -DCMAKE_BUILD_TYPE=Debug -DSTATIC_LINKING=1 -DSTATIC_LIBC=1