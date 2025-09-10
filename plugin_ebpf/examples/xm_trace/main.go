/*
 * @Author: CALM.WU
 * @Date: 2022-12-28 10:42:19
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-09-10 15:14:09
 */

package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	goflag "flag"
	"fmt"
	"reflect"
	"strings"
	"time"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/asm"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/cilium/ebpf/rlimit"
	"github.com/golang/glog"
	"xmonitor.calmwu/plugin_ebpf/examples/xm_trace/bpfmodule"

	calmutils "github.com/wubo0067/calmwu-go/utils"
)

const (
	_perf_max_stack_depth = 127
)

var (
	__pid         int
	__kprobeFuncs string
)

func init() {
	goflag.IntVar(&__pid, "pid", 0, "trace process syscalls pid")
	goflag.StringVar(&__kprobeFuncs, "funcs", "", "trace kprobe funcs, example: --funcs=sys_close,sys_readlinkat,sys_openat")
}

func __fillGetCallStackInstructions(progSpec *ebpf.ProgramSpec) {
	progSpec.Instructions = asm.Instructions{
		// 0:	r6 = r1, MovReg dst: r6 src: r1
		asm.Mov.Reg(asm.R6, asm.R1),
		// 1:	call 14, Call FnGetCurrentPidTgid
		asm.FnGetCurrentPidTgid.Call(),
		// 2:	r7 = r0, MovReg dst: r7 src: r0
		asm.Mov.Reg(asm.R7, asm.R0),
		// 3:	call 14, Call FnGetCurrentPidTgid
		asm.FnGetCurrentPidTgid.Call(),
		// 4:	*(u32 *)(r10 - 4) = r0, StXMemW dst: rfp src: r0 off: -4 imm: 0
		asm.StoreMem(asm.RFP, -4, asm.R0, asm.Word),
		// 5:	r1 = 0 ll, LoadMapValue dst: r1, fd: 0 off: 0 <.rodata>
		asm.LoadMapValue(asm.R1, 0, 0).WithReference(".rodata"),
		// 7:	r2 = *(u32 *)(r1 + 0), LdXMemW dst: r2 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R2, asm.R1, 0, asm.Word),
		// 8:	if r2 == 0 goto +36 <LBB1_8>, JEqImm dst: r2 off: 36 imm: 0
		func() asm.Instruction {
			ins := asm.JEq.Imm(asm.R2, 0, "") // !! 实际中不需要加上 Label，只需要填写 Offset
			ins.Offset = 36
			return ins
		}(),
		// asm.JEq.Imm(asm.R2, 0, "LBB1_8"),
		// 9:	r1 = *(u32 *)(r1 + 0), LdXMemW dst: r1 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R1, asm.R1, 0, asm.Word),
		// 10:	if r1 == 0 goto +5 <LBB1_3>, JEqImm dst: r1 off: 5 imm: 0
		func() asm.Instruction {
			ins := asm.JEq.Imm(asm.R1, 0, "")
			ins.Offset = 5
			return ins
		}(),
		// 11:	r7 >>= 32, RShImm dst: r7 imm: 32
		asm.RSh.Imm(asm.R7, 32),
		// 12:	r1 = 0 ll, LoadMapValue dst: r1, fd: 0 off: 0 <.rodata>
		asm.LoadMapValue(asm.R1, 0, 0).WithReference(".rodata"),
		// 14:	r1 = *(u32 *)(r1 + 0), LdXMemW dst: r1 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R1, asm.R1, 0, asm.Word),
		// 15:	if r1 != r7 goto +29 <LBB1_8>, JNEReg dst: r1 off: 29 src: r7
		func() asm.Instruction {
			ins := asm.JNE.Reg(asm.R1, asm.R7, "")
			ins.Offset = 29
			return ins
		}(),
		// 0000000000000080 <LBB1_3>:
		// 16:	r2 = r10, MovReg dst: r2 src: rfp
		asm.Mov.Reg(asm.R2, asm.RFP),
		// 17:	r2 += -4, AddImm dst: r2 imm: -4
		asm.Add.Imm(asm.R2, -4),
		// 18:	r1 = 0 ll, LoadMapPtr dst: r1 fd: 0 <xm_syscalls_record_map>
		asm.LoadMapPtr(asm.R1, 0).WithReference("xm_syscalls_record_map"),
		// 20:	call 1, Call FnMapLookupElem
		asm.FnMapLookupElem.Call(),
		// 21:	r7 = r0, MovReg dst: r7 src: r0
		asm.Mov.Reg(asm.R7, asm.R0),
		// 22:	if r7 == 0 goto +22 <LBB1_8>, JEqImm dst: r7 off: 22 imm: 0
		func() asm.Instruction {
			ins := asm.JEq.Imm(asm.R7, 0, "")
			ins.Offset = 22
			return ins
		}(),
		// 23:	r1 = r6, MovReg dst: r1 src: r6
		asm.Mov.Reg(asm.R1, asm.R6),
		// 24:	r2 = 0 ll, LoadMapPtr dst: r2 fd: 0 <xm_syscalls_stack_map>
		asm.LoadMapPtr(asm.R2, 0).WithReference("xm_syscalls_stack_map"),
		// 26:	r3 = 512, MovImm dst: r3 imm: 512
		asm.Mov.Imm(asm.R3, 512),
		// 27:	call 27, Call FnGetStackid
		asm.FnGetStackid.Call(),
		// 28:	r8 = r0, MovReg dst: r8 src: r0
		asm.Mov.Reg(asm.R8, asm.R0),
		// 29:	r1 = r6, MovReg dst: r1 src: r6
		asm.Mov.Reg(asm.R1, asm.R6),
		// 30:	r2 = 0 ll, LoadMapPtr dst: r2 fd: 0 <xm_syscalls_stack_map>
		asm.LoadMapPtr(asm.R2, 0).WithReference("xm_syscalls_stack_map"),
		// 32:	r3 = 768, MovImm dst: r3 imm: 768
		asm.Mov.Imm(asm.R3, 768),
		// 33:	call 27, Call FnGetStackid
		asm.FnGetStackid.Call(),
		// 34:	r2 = r8, MovReg dst: r2 src: r8
		asm.Mov.Reg(asm.R2, asm.R8),
		// 35:	r2 <<= 32, LShImm dst: r2 imm: 32
		asm.LSh.Imm(asm.R2, 32),
		// 36:	r2 s>>= 32, ArShImm dst: r2 imm: 32
		asm.ArSh.Imm(asm.R2, 32),
		// 37:	r1 = 1, MovImm dst: r1 imm: 1
		asm.Mov.Imm(asm.R1, 1),
		// 38:	if r1 s> r2 goto +1 <LBB1_6>, JSGTReg dst: r1 off: 1 src: r2
		func() asm.Instruction {
			ins := asm.JSGT.Reg(asm.R1, asm.R2, "")
			ins.Offset = 1
			return ins
		}(),
		// 39:	*(u32 *)(r7 + 40) = r8, StXMemW dst: r7 src: r8 off: 40 imm: 0
		asm.StoreMem(asm.R7, 40, asm.R8, asm.Word),
		// 0000000000000140 <LBB1_6>:
		// 40:	r2 = r0, MovReg dst: r2 src: r0
		asm.Mov.Reg(asm.R2, asm.R0),
		// 41:	r2 <<= 32, LShImm dst: r2 imm: 32
		asm.LSh.Imm(asm.R2, 32),
		// 42:	r2 s>>= 32, ArShImm dst: r2 imm: 32
		asm.ArSh.Imm(asm.R2, 32),
		// 43:	if r1 s> r2 goto +1 <LBB1_8>, JSGTReg dst: r1 off: 1 src: r2
		func() asm.Instruction {
			ins := asm.JSGT.Reg(asm.R1, asm.R2, "")
			ins.Offset = 1
			return ins
		}(),
		// 44:	*(u32 *)(r7 + 44) = r0, StXMemW dst: r7 src: r0 off: 44 imm: 0
		asm.StoreMem(asm.R7, 44, asm.R0, asm.Word),
		// 0000000000000168 <LBB1_8>:
		// 45:	r0 = 0, MovImm dst: r0 imm: 0
		asm.Mov.Imm(asm.R0, 0),
		// 46:	exit, Exit
		asm.Return(),
	}
}

func __generateKprobeProgramSpecsForGetCallStack(name, sectionName string) *ebpf.ProgramSpec {
	progSpec := new(ebpf.ProgramSpec)
	progSpec.Name = name
	progSpec.Type = ebpf.Kprobe
	progSpec.License = "GPL"
	progSpec.SectionName = sectionName
	__fillGetCallStackInstructions(progSpec)

	// glog.Info("\n", progSpec.Instructions.String())

	return progSpec
}

// This function generates a list of ebpf programs to trace the call stack of the function specified by the kprobeFuncLst parameter.
// The parameters ebpfMaps and progLst are used to associate ebpf maps and output ebpf programs.
func __generateKprobeProgramsForGetCallStack(kprobeFuncLst []string, ebpfMaps map[string]*ebpf.Map) []*ebpf.Program {
	var progLst []*ebpf.Program
	for _, kprobeFunc := range kprobeFuncLst {
		sectionName := fmt.Sprintf("kprobe/%s", kprobeFunc)
		progSpec := __generateKprobeProgramSpecsForGetCallStack(kprobeFunc, sectionName)
		// associate map
		for name, ebpfMap := range ebpfMaps {
			progSpec.Instructions.AssociateMap(name, ebpfMap)
		}
		prog, err := ebpf.NewProgram(progSpec)
		if err != nil {
			glog.Fatalf("failed to create ebpf program for '%s': %v", kprobeFunc, err)
		} else {
			glog.Infof("create ebpf program:'%s', sectionName:'%s' success", kprobeFunc, sectionName)
		}

		progLst = append(progLst, prog)
	}

	return progLst
}

func main() {
	goflag.Parse()
	defer glog.Flush()

	glog.Infof("start trace process:'%d' syscalls", __pid)

	// 解析__kprobeFuncs
	kprobeFuncLst := strings.Split(__kprobeFuncs, ",")
	if len(kprobeFuncLst) == 0 {
		glog.Fatal("kprobe funcs is empty")
	}

	// 加载 kernel、进程的符号
	calmutils.InitModuleSymbolTblMgr(128)
	calmutils.LoadKallSyms()
	procSyms, _ := calmutils.NewProcMaps(__pid)

	if err := rlimit.RemoveMemlock(); err != nil {
		glog.Fatalf("failed to remove memlock limit: %v", err)
	}

	//-------------------------------------------------------------------------

	// 加载字节码，生成 ebpf.CollectionSpec，包括 MapSpec 和 ProgramSpec
	spec, err := bpfmodule.LoadXMTrace()
	if err != nil {
		glog.Fatalf("failed to invoke LoadXMTrace, err: %v", err)
	}

	//-------------------------------------------------------------------------

	// rewrite .rodata, bpftool btf dump file xm_trace_syscalls.bpf.o
	// const 变量会放到.rodata section 中
	// !! 如果不把提前修改.rodata 数据，加载的指令就会被优化掉，直接 return 了，使用 bpftool prog dump xlated id 2563 只能看到部分指令
	//err = spec.RewriteConstants(map[string]interface{}{
	err = spec.Variables["xm_trace_syscall_filter_pid"].Set(int32(__pid))
	if err != nil {
		glog.Fatalf("failed to rewrite constants xm_trace_syscall_filter_pid, err: %v", err)
	}

	//-------------------------------------------------------------------------
	// 输出 spec 中所有的 map
	for mapK := range spec.Maps {
		glog.Infof("map name: %s", mapK)
	}

	// 构造.rodata ebpf.Map 对象
	rodataMap, err := ebpf.NewMap(spec.Maps[".rodata"])
	if err != nil {
		glog.Fatalf("failed to create .rodata map, err: %v", err)
	}
	defer rodataMap.Close()

	//-------------------------------------------------------------------------
	// 通过 spec 构造 program 和 map
	objs := bpfmodule.XMTraceObjects{}
	err = spec.LoadAndAssign(&objs, nil)
	if err != nil {
		glog.Fatalf("failed to load XMTraceObjects, err: %v", err)
	}

	// glog.Infof("XmBtfRtpSysEnter name:%s type:%d", objs.XmTraceRawTpSysEnter.String(), objs.XmTraceRawTpSysEnter.Type())

	defer objs.Close()

	//-------------------------------------------------------------------------

	// 通过 reflect 来获取 objs.Programs 的所有成员，统一 ebpf Program 的 attach
	var ebpfLinks []link.Link

	bpfProgs := reflect.Indirect(reflect.ValueOf(objs.XMTracePrograms))
	bpfProgsT := bpfProgs.Type()

	numFields := bpfProgsT.NumField()
	for i := 0; i < numFields; i++ {
		bpfProgS := bpfProgsT.Field(i)                  // reflect.StructField
		bpfProgV := bpfProgs.FieldByName(bpfProgS.Name) // 成员具体值

		// glog.Infof("epbf Prog name[%s] type:[%s] value:[%v] valueType:[%v] kind:%v\n",
		// 	bpfProgS.Name, bpfProgS.Type.String(), bpfProgV, bpfProgV.Type(), bpfProgS.Type.Kind())

		if bpfProgS.Type.Kind() == reflect.Ptr {
			bpfProg := bpfProgV.Interface().(*ebpf.Program)

			progStr := bpfProg.String()
			startPos := strings.Index(progStr, "__")
			endPos := strings.LastIndexByte(progStr, ')')
			progSecSubName := progStr[startPos+2 : endPos]

			glog.Infof("prog:'%s' ebpf info:'%s'", bpfProgS.Name, bpfProg.String())

			switch bpfProg.Type() {
			case ebpf.Kprobe:
				// attach kprobe program
				linkKP, err := link.Kprobe(progSecSubName, bpfProg, nil)
				if err != nil {
					glog.Fatalf("failed to attach %s program for link, error: %v", bpfProg.String(), err)
				} else {
					glog.Infof("attach KProbe %s program for link success.", bpfProg.String())
					ebpfLinks = append(ebpfLinks, linkKP)
				}
			case ebpf.RawTracepoint:
				linkRawTP, err := link.AttachRawTracepoint(link.RawTracepointOptions{Name: progSecSubName, Program: bpfProg})
				if err != nil {
					glog.Fatalf("failed to attach %s program for link, error: %v", bpfProg.String(), err)
				} else {
					glog.Infof("attach RawTracepoint %s program for link success.", bpfProg.String())
					ebpfLinks = append(ebpfLinks, linkRawTP)
				}
			// case ebpf.TracePoint:
			// 要获取 group、name
			// tp, err := link.Tracepoint("syscalls", "sys_enter_openat", prog, nil)
			case ebpf.Tracing:
				linkTracing, err := link.AttachTracing(link.TracingOptions{Program: bpfProg})
				if err != nil {
					glog.Fatalf("failed to attach %s program for link, error: %v", bpfProg.String(), err)
				} else {
					glog.Infof("attach BTFRawTracepoint %s program for link success.", bpfProg.String())
					ebpfLinks = append(ebpfLinks, linkTracing)
				}
			default:
				glog.Fatalf("unsupported ebpf type: %d", bpfProg.Type())
			}
		}
	}
	//-------------------------------------------------------------------------

	// 创建自定义的 ebpf.Program
	// kprobeStackProgSpec := __generateKprobeProgramSpecsForGetCallStack("sys_close", "kprobe/sys_close")
	// // 借用现有 program 的指令
	// // kprobeStackProgSpec.Instructions = make([]asm.Instruction, len(spec.Programs["xm_trace_kp__sys_readlinkat"].Instructions))
	// // copy(kprobeStackProgSpec.Instructions, spec.Programs["xm_trace_kp__sys_readlinkat"].Instructions)
	// // glog.Info("\n", kprobeStackProgSpec.Instructions.String())

	// kprobeStackProgSpec.Instructions.AssociateMap("xm_syscalls_stack_map", objs.XmSyscallsStackMap)
	// kprobeStackProgSpec.Instructions.AssociateMap("xm_syscalls_record_map", objs.XmSyscallsRecordMap)
	// kprobeStackProgSpec.Instructions.AssociateMap(".rodata", rodataMap)

	// kprobeStackProg, err := ebpf.NewProgram(kprobeStackProgSpec)
	// if err != nil {
	// 	glog.Fatalf("failed to create kprobeStackProg, err: %v", err)
	// }

	// defer kprobeStackProg.Close()

	ebpfMaps := map[string]*ebpf.Map{
		"xm_syscalls_stack_map":  objs.XmSyscallsStackMap,
		"xm_syscalls_record_map": objs.XmSyscallsRecordMap,
		".rodata":                rodataMap,
	}

	kprobeGetCallStackProgs := __generateKprobeProgramsForGetCallStack(kprobeFuncLst, ebpfMaps)
	defer func() {
		for _, prog := range kprobeGetCallStackProgs {
			prog.Close()
		}
	}()

	// link attach kprobe GetCallStack Progs
	for i, kprobeFunc := range kprobeFuncLst {
		prog := kprobeGetCallStackProgs[i]
		l, err := link.Kprobe(kprobeFunc, prog, nil)
		if err != nil {
			glog.Fatalf("failed to attach kprobe %s program for link, error: %v", prog.String(), err)
		} else {
			glog.Infof("attach KProbe %s program for link success.", prog.String())
			ebpfLinks = append(ebpfLinks, l)
		}

	}
	// link_kp_sys_close, err := link.Kprobe("sys_close", kprobeStackProg, nil)
	// if err != nil {
	// 	glog.Fatalf("failed to attach kprobe %s program for link, error: %v", kprobeStackProg.String(), err)
	// }
	// defer link_kp_sys_close.Close()

	//-------------------------------------------------------------------------

	defer func() {
		for i, link := range ebpfLinks {
			glog.Infof("%d link detached", i)
			link.Close()
		}
	}()

	//-------------------------------------------------------------------------

	// 注册信号
	ctx := calmutils.SetupSignalHandler()

	// attach btf raw tracepoint program for link
	// link_sys_enter, err := link.AttachTracing(link.TracingOptions{Program: objs.XmTraceRtpSysEnter})

	// attach raw_tracepoint program
	// link_sys_enter, err := link.AttachRawTracepoint(link.RawTracepointOptions{Name: "sys_enter", Program: objs.XmTraceRawTpSysEnter})
	// if err != nil {
	// 	glog.Fatalf("failed to attach %s program for link, error: %v", objs.XmTraceRawTpSysEnter.String(), err)
	// }
	// defer link_sys_enter.Close()

	// // link_sys_exit, err := link.AttachTracing(link.TracingOptions{Program: objs.XmTraceRtpSysExit})
	// link_sys_exit, err := link.AttachRawTracepoint(link.RawTracepointOptions{Name: "sys_exit", Program: objs.XmTraceRawTpSysExit})
	// if err != nil {
	// 	glog.Fatalf("failed to attach %s program for link, error: %v", objs.XmTraceRawTpSysExit.String(), err)
	// }
	// defer link_sys_exit.Close()

	// link_kp_sys_readlinkat, err := link.Kprobe("sys_readlinkat", objs.XmTraceKpSysReadlinkat, nil)
	// if err != nil {
	// 	glog.Fatalf("failed to attach kprobe %s program for link, error: %v", objs.XmTraceKpSysReadlinkat.String(), err)
	// }
	// defer link_kp_sys_readlinkat.Close()

	// link_kp_sys_openat, err := link.Kprobe("sys_openat", objs.XmTraceKpSysOpenat, nil)
	// if err != nil {
	// 	glog.Fatalf("failed to attach kprobe %s program for link, error: %v", objs.XmTraceKpSysOpenat.String(), err)
	// }
	// defer link_kp_sys_openat.Close()

	// link_kp_sys_close, err := link.Kprobe("sys_close", objs.XmTraceKpSysClose, nil)
	// if err != nil {
	// 	glog.Fatalf("failed to attach kprobe %s program for link, error: %v", objs.XmTraceKpSysClose.String(), err)
	// }
	// defer link_kp_sys_close.Close()

	//-------------------------------------------------------------------------

	evtRD, err := ringbuf.NewReader(objs.XmSyscallsEventMap)
	if err != nil {
		glog.Fatalf("failed to create ringbuf reader, error: %v", err)
	}
	defer evtRD.Close()

	go func() {
		<-ctx.Done()

		if err := evtRD.Close(); err != nil {
			glog.Fatalf("failed to close ringbuf reader, error: %v", err)
		}
	}()

	glog.Infof("Start receiving events...")
	// stackFrameSize := (strconv.IntSize / 8)

	var syscallEvt bpfmodule.XMTraceSyscallEvent
	for {
		record, err := evtRD.Read()
		if err != nil {
			if errors.Is(err, ringbuf.ErrClosed) {
				glog.Warningln("Received signal, exiting...")
				return
			}
			glog.Errorf("reading from ringbuf failed, err: %v", err)
			continue
		}

		// parse the ringbuf record into XMTraceSyscallEvent struct
		if err := binary.Read(bytes.NewBuffer(record.RawSample), binary.LittleEndian, &syscallEvt); err != nil {
			glog.Errorf("failed to parse syscall event, err: %v", err)
			continue
		}

		// float64(syscallEvt.DelayNs)/float64(time.Millisecond)
		glog.Infof("pid:%d, tid:%d, (%f ms) syscall_nr:%d(%s) = %d",
			syscallEvt.Pid, syscallEvt.Tid, float64(syscallEvt.CallDelayNs)/float64(time.Millisecond),
			syscallEvt.SyscallNr, func() string {
				syscallStr, _ := calmutils.ConvertSysCallNR2Str_X86_64(int(syscallEvt.SyscallNr))
				return syscallStr
			}(), syscallEvt.SyscallRet)

		var stackAddrs [_perf_max_stack_depth]uint64

		if syscallEvt.KernelStackId > 0 {
			// stackBytes, err := objs.XmSyscallsStackMap.LookupBytes(syscallEvt.StackId)
			err = objs.XmSyscallsStackMap.Lookup(syscallEvt.KernelStackId, &stackAddrs)
			if err != nil {
				glog.Errorf("failed to lookup stack_id: %d, err: %v", syscallEvt.KernelStackId, err)
			} else {
				// for i := 0; i < _perf_max_stack_depth; i++ {
				for i, stackAddr := range stackAddrs {
					// for i := 0; i < len(stackBytes); i += stackFrameSize {
					// stackAddr := binary.LittleEndian.Uint64(stackBytes[i : i+stackFrameSize])
					if stackAddr == 0 {
						continue
					}
					sym, err := calmutils.FindKsym(stackAddr)
					if err != nil {
						sym = "?"
					}
					glog.Infof("\tip[%d]: 0x%016x\t%s", i, stackAddr, sym)
				}
			}
		}

		if syscallEvt.UserStackId > 0 {
			err = objs.XmSyscallsStackMap.Lookup(syscallEvt.UserStackId, &stackAddrs)
			if err != nil {
				glog.Errorf("failed to lookup stack_id: %d, err: %v", syscallEvt.UserStackId, err)
			} else {
				// for i := 0; i < _perf_max_stack_depth; i++ {
				for i, stackAddr := range stackAddrs {
					// for i := 0; i < len(stackBytes); i += stackFrameSize {
					// stackAddr := binary.LittleEndian.Uint64(stackBytes[i : i+stackFrameSize])
					if stackAddr == 0 {
						continue
					}
					sym, offset, moduleName, err := procSyms.ResolvePC(stackAddr)
					if err != nil {
						sym = "?"
					}
					glog.Infof("\tip[%d]: 0x%016x\t%s+0x%x [%s]", i, stackAddr, sym, offset, moduleName)
				}
			}
		}

		// objs.XmSyscallsStackMap.Delete(syscallEvt.StackId)
	}

	glog.Infof("trace process syscalls exit")
}

/*
trace '__x64_sys_bpf' -K -U -T -a
dlv exec ./xm_trace -- --pid=3638918 --alsologtostderr -v=4
./xm_trace --pid=3638918 --funcs=sys_close,sys_readlinkat,sys_openat --alsologtostderr -v=4
trace '__x64_sys_openat' -K -T -a -p 342284
llvm-objdump -d -S --no-show-raw-insn  --symbolize-operands xm_trace.bpf.o

llvm-objdump -d -S --no-show-raw-insn  --symbolize-operands ../plugin_ebpf/examples/xm_trace/bpfmodule/xmtrace_bpfel.o

llvm-objdump -d -S --no-show-raw-insn  --symbolize-operands ./xmtrace_bpfeb.o
./xm_progspec --elf=../plugin_ebpf/examples/xm_trace/bpfmodule/xmtrace_bpfel.o --alsologtostderr -v=4
*/
