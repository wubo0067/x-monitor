/*
 * @Author: calmwu
 * @Date: 2022-07-14 22:38:50
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2023-02-09 14:53:39
 */

package main

import (
	"runtime/debug"

	"go.uber.org/automaxprocs/maxprocs"
	"xmonitor.calmwu/plugin_ebpf/exporter/cmd"
)

// Main is the entry point for the command
func main() {
	debug.SetGCPercent(50)

	undo, _ := maxprocs.Set()
	defer undo()
	cmd.Main()
}

// ./x-monitor.ebpf --config=../config/xm_ebpf_plugin/config.yaml --log_dir=/var/log/x-monitor/ --v=5
// dlv exec ./x-monitor.ebpf -- --config=../config/xm_ebpf_plugin/config.yaml --log_dir=/var/log/x-monitor/ --v=5
