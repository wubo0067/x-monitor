/*
 * @Author: CALM.WU
 * @Date: 2023-02-17 13:58:55
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-03-04 16:05:55
 */

package bpfmodule

// //go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type cachestat_top_statistics XMCacheStatTop ../../../bpf/xm_cachestat_top.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_runqlat_hist XMRunQLat ../../../bpf/xm_runqlat.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" XMCacheStat ../../../bpf/xm_cachestat.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_runqlat_hist -type xm_cpu_sched_evt_type -type xm_cpu_sched_evt_data XMCpuSchedule ../../../bpf/xm_cpu_sched.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_processvm_evt_type -type xm_processvm_evt_data XMProcessVM ../../../bpf/xm_process_vm.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_oomkill_evt_data XMOomKill ../../../bpf/xm_oomkill.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_bio_key -type xm_bio_data -type xm_bio_request_latency_evt_data XMBio ../../../bpf/xm_bio.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_blk_num -type xm_nfs_oplat_stat XMNfs ../../../bpf/xm_nfs.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include
//go:generate env GOPACKAGE=bpfmodule bpf2go -cc clang -no-strip -cflags "-g -O2 -Wall -Wno-unused-function -D__TARGET_ARCH_x86 -DOUTPUT_SKB" -type xm_profile_sample -type xm_profile_sample_data -type xm_prog_filter_args -type xm_prog_filter_target_scope_type -type xm_profile_fde_table_row -type xm_profile_fde_table_info -type xm_profile_proc_maps_module -type xm_profile_pid_maps -type xm_profile_module_fde_tables -type xm_ehframe_user_stack XMProfile ../../../bpf/xm_profile.bpf.c -- -I../../../ -I../../../bpf/.output -I../../../../extra/include

//go:generate genny -in=../../../../vendor/github.com/wubo0067/calmwu-go/utils/generic_channel.go -out=gen_xm_cs_sched_evt_data_channel.go -pkg=bpfmodule gen "ChannelCustomType=*XMCpuScheduleXmCpuSchedEvtData ChannelCustomName=CpuSchedEvtData"
//go:generate genny -in=../../../../vendor/github.com/wubo0067/calmwu-go/utils/generic_channel.go -out=gen_xm_process_vm_evt_data_channel.go -pkg=bpfmodule gen "ChannelCustomType=*XMProcessVMXmProcessvmEvtData ChannelCustomName=ProcessVMEvtData"
//go:generate genny -in=../../../../vendor/github.com/wubo0067/calmwu-go/utils/generic_channel.go -out=gen_xm_oomkill_evt_data_channel.go -pkg=bpfmodule gen "ChannelCustomType=*XMOomKillXmOomkillEvtData ChannelCustomName=OomkillEvtData"
//go:generate genny -in=../../../../vendor/github.com/wubo0067/calmwu-go/utils/generic_channel.go -out=gen_xm_bio_req_latency_evt_data_channel.go -pkg=bpfmodule gen "ChannelCustomType=*XMBioXmBioRequestLatencyEvtData ChannelCustomName=BioRequestLatencyEvtData"

//go:generate stringer -type=XMProcessVMXmProcessvmEvtType -output=processvm_evt_type_string.go
