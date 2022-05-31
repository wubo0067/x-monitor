/*
 * @Author: CALM.WU
 * @Date: 2022-05-23 11:41:05
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-05-23 12:05:47
 */

package main

import (
	"flag"
	"syscall"
	"time"

	"github.com/golang/glog"
	calmutils "github.com/wubo0067/calmwu-go/utils"
)

func create_socket() error {
	return nil
}

func main() {
	flag.Parse()

	glog.Info("start bpftrace socket")

	var fd_slice []int
	ctx := calmutils.SetupSignalHandler()

L:
	for {
		select {
		case <-ctx.Done():
			glog.Info("stop bpftrace socket")
			break L
		case <-time.After(time.Second * 3):
			fd, err := syscall.Socket(syscall.AF_INET, syscall.SOCK_STREAM, syscall.IPPROTO_IP)
			if err != nil {
				glog.Errorf("create socket error: %s", err.Error())
			} else {
				fd_slice = append(fd_slice, fd)
				glog.Infof("create socket fd: %d", fd)
			}
		}
	}

	for _, fd := range fd_slice[1:] {
		syscall.Close(fd)
		glog.Infof("close socket fd: %d", fd)
	}

	time.Sleep(3 * time.Second)
	glog.Info("bpftrace socket exit!")
}
