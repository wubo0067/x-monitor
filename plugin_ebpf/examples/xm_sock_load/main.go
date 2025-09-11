/*
 * @Author: CALM.WU
 * @Date: 2025-09-10 14:42:02
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-10 15:15:30
 */

package main

import (
	"flag"
	"fmt"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/rlimit"
	"github.com/golang/glog"
)

//go:generate env GOPACKAGE=main bpf2go -no-strip -cc clang XMCGSockProg ../../bpf/xm_sock_flags.bpf.c -- -I../../bpf/.output -I../../../extra/include/bpf -g -Wall -Werror -Wno-unused-function -D__TARGET_ARCH_x86

const (
	lnkCGPinPath = "/sys/fs/bpf/xm_sock_flags"
)

func selectProgram(objs *XMCGSockProgObjects, filterID int) (*ebpf.Program, error) {
	switch filterID {
	case 1:
		return objs.XmSockProg1, nil
	case 2:
		return objs.XmSockProg2, nil
	default:
		return nil, fmt.Errorf("invalid progFilterID: %d", filterID)
	}
}

var (
	// cgroupPath is the path to the cgroup to attach to
	cgroupPath string
	// progFilterID is the program filter ID
	progFilterID int
)

func init() {
	flag.StringVar(&cgroupPath, "cgrpPath", "/tmp/cgroupv2/foo", "cgroup path to attach")
	flag.IntVar(&progFilterID, "progFilterID", 1, "prog filter id")
}

func main() {
	var err error
	var lnkCG link.Link

	flag.Parse()
	defer glog.Flush()

	// 判断参数个数，如果参数数量不为 3，报错退出
	if cgroupPath == "" || progFilterID <= 0 {
		glog.Fatalf("Usage: xm_sock_flags -cgrpPath <cgroup path> -progFilterID <1/2>")
	}

	glog.Infof("Starting xm_sock_flags program, cgrpPath: %s, progFilterID: %d", cgroupPath, progFilterID)

	if err := rlimit.RemoveMemlock(); err != nil {
		glog.Fatal(err)
	}

	objs := XMCGSockProgObjects{}
	if err = LoadXMCGSockProgObjects(&objs, nil); err != nil {
		glog.Fatalf("loading objects: %v", err)
	}
	defer objs.Close()

	// Select program based on filter ID
	program, err := selectProgram(&objs, progFilterID)
	if err != nil {
		glog.Fatalf("Failed to select program: %v", err)
	}

	// Attach to cgroup
	lnkCG, err = link.AttachCgroup(link.CgroupOptions{
		Path:    cgroupPath,
		Attach:  ebpf.AttachCGroupInetSockCreate,
		Program: program,
	})
	if err != nil {
		glog.Fatalf("Failed to attach cgroup: %v", err)
	}
	defer lnkCG.Close()

	// Pin the link
	if err = lnkCG.Pin(lnkCGPinPath); err != nil {
		glog.Fatalf("pinning link: %v", err)
	}

	glog.Infof("Successfully attached program and pinned link to %s", lnkCGPinPath)

}

/*
rm -rf /sys/fs/bpf/xm_sock_flags
*/
