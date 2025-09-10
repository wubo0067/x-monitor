/*
 * @Author: CALM.WU
 * @Date: 2025-09-10 14:42:02
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-10 15:15:30
 */

package main

import (
	goflag "flag"

	"github.com/cilium/ebpf/rlimit"
	"github.com/golang/glog"
)

//go:generate env GOPACKAGE=main bpf2go -no-strip -cc clang XMCGSockProg ../../bpf/xm_sock_flags.bpf.c -- -I../../bpf/.output -I../../../extra/include/bpf -g -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86

var (
	// cgroup path to attach
	__cgrpPath string
	// prog filter id
	__progFilterID int = -1
)

func init() {
	goflag.StringVar(&__cgrpPath, "cgrpPath", "/sys/fs/cgroup/unified/xm_sock_flags", "cgroup path to attach")
	goflag.IntVar(&__progFilterID, "progFilterID", -1, "prog filter id")
}

func main() {
	goflag.Parse()
	defer glog.Flush()

	glog.Infoln("Starting xm_sock_flags program")

	if err := rlimit.RemoveMemlock(); err != nil {
		glog.Fatal(err)
	}
}
