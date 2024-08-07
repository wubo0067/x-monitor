/*
 * @Author: CALM.WU
 * @Date: 2023-01-16 11:06:19
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2023-12-21 14:48:35
 */

package main

import (
	goflag "flag"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/asm"
	"github.com/cilium/ebpf/btf"
	"github.com/golang/glog"
)

var __elfFile string
var __btfFile string

func init() {
	goflag.StringVar(&__elfFile, "elf", "../plugin_ebpf/bpf/.output/xm_trace.bpf.o", "llvm clang generate elf file")
	goflag.StringVar(&__btfFile, "btf", "../plugin_ebpf/btf/xm-ol8.7-4.18.0-425.19.2.el8_7.x86_64.btf", "ebpf program co-re btf file")
}

func __generate_asm_insns() {
	// 翻译 llvm-objdump -d -s --no-show-raw-insn  --symbolize-operands xm_trace.bpf.o 导出的指令，**但这不能被 cilium ebpf 加载
	insns := asm.Instructions{
		// 0:	*(u64 *)(r10 - 16) = r1, StXMemDW dst: rfp src: r1 off: -16 imm: 0     rfp = r10
		asm.StoreMem(asm.RFP, -16, asm.R1, asm.DWord),
		// 1:	r1 = 64 ll, LoadMapValue dst: r1, fd: 0 off: 64 <.data>
		asm.LoadMapValue(asm.R1, 0, 64).WithReference(".data"),
		// 3:	*(u64 *)(r10 - 48) = r1, StXMemDW dst: rfp src: r1 off: -48 imm: 0
		asm.StoreMem(asm.RFP, -48, asm.R1, asm.DWord),
		// 4:	r1 = *(u64 *)(r1 + 0), LdXMemDW dst: r1 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R1, asm.R1, 0, asm.DWord),
		// 5:	callx r1, Call FnMapLookupElem
		asm.FnMapLookupElem.Call(),
		// 6:	r1 = *(u64 *)(r10 - 48), LdXMemDW dst: r1 src: rfp off: -48 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -48, asm.DWord),
		// 7:	r0 >>= 32, RShImm dst: r0 imm: 32
		asm.RSh.Imm(asm.R0, 32),
		// 8:	*(u32 *)(r10 - 20) = r0, StXMemW dst: rfp src: r0 off: -20 imm: 0
		asm.StoreMem(asm.RFP, -20, asm.R0, asm.Word),
		// r1 = *(u64 *)(r1 + 0), LdXMemDW dst: r1 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R1, asm.R1, 0, asm.DWord),
		// 10:	callx r1, Call FnMapLookupElem
		asm.FnMapLookupElem.Call(),
		// 11:	*(u32 *)(r10 - 24) = r0, StXMemW dst: rfp src: r0 off: -24 imm: 0
		asm.StoreMem(asm.RFP, -24, asm.R0, asm.Word),
		// 12:	r1 = 0 ll, LoadMapValue dst: r1, fd: 0 off: 0 <.rodata>
		asm.LoadMapValue(asm.R1, 0, 0).WithReference(".rodata"),
		// 14:	r1 = *(u32 *)(r1 + 0), LdXMemW dst: r1 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R1, asm.R1, 0, asm.Word),
		// 15:	if r1 == 0 goto +12 <LBB0_3>, JEqImm dst: r1 off: 12 imm: 0
		asm.JEq.Imm(asm.R1, 0, "LBB0_3"),
		// 16:	goto +0 <LBB0_1>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_1"),
		// 17:	r1 = 0 ll, LoadMapValue dst: r1, fd: 0 off: 0 <.rodata>
		asm.LoadMapValue(asm.R1, 0, 0).WithSymbol("LBB0_1").WithReference(".rodata"),
		// 19:	r1 = *(u32 *)(r1 + 0), LdXMemW dst: r1 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R1, asm.R1, 0, asm.Word),
		// if r1 == 0 goto +10 <LBB0_4>, JEqImm dst: r1 off: 10 imm: 0
		asm.JEq.Imm(asm.R1, 0, "LBB0_4"),
		// 21:	goto +0 <LBB0_2>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_2"),
		// 22:	r1 = *(u32 *)(r10 - 20), LdXMemW dst: r1 src: rfp off: -20 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -20, asm.Word).WithSymbol("LBB0_2"),
		// 23:  r2 = 0 ll, LoadMapValue dst: r2, fd: 0 off: 0 <.rodata>
		asm.LoadMapValue(asm.R2, 0, 0).WithReference(".rodata"),
		// 25:	r2 = *(u32 *)(r2 + 0), LdXMemW dst: r2 src: r2 off: 0 imm: 0
		asm.LoadMem(asm.R2, asm.R2, 0, asm.Word),
		// 26:	if r1 == r2 goto +4 <LBB0_4>, JEqReg dst: r1 off: 4 src: r2
		asm.JEq.Reg(asm.R1, asm.R2, "LBB0_4"),
		// 27:	goto +0 <LBB0_3>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_3"),
		// 28:	r1 = 0, MovImm dst: r1 imm: 0
		asm.Mov.Imm(asm.R1, 0).WithSymbol("LBB0_3"),
		// 29:	*(u32 *)(r10 - 4) = r1, StXMemW dst: rfp src: r1 off: -4 imm: 0
		asm.StoreMem(asm.RFP, -4, asm.R1, asm.Word),
		// 30:	goto +54 <LBB0_11>, JaImm dst: r0 off: 54 imm: 0
		asm.Ja.Label("LBB0_11"),
		// 31:	r1 = 0 ll, LoadMapValue dst: r1, fd: 0 off: 0 <.data>
		asm.LoadMapValue(asm.R1, 0, 0).WithSymbol("LBB0_4").WithReference(".rodata"),
		// 33:	r3 = *(u64 *)(r1 + 0), LdXMemDW dst: r3 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R3, asm.R1, 0, asm.DWord),
		// 34:	r1 = 0 ll, LoadMapPtr dst: r1 fd: 0 <xm_syscalls_record_map>
		asm.LoadMapPtr(asm.R1, 0).WithReference("xm_syscalls_record_map"),
		// 36:	r2 = r10, MovReg dst: r2 src: rfp
		asm.Mov.Reg(asm.R2, asm.RFP),
		// 37:	r2 += -24, AddImm dst: r2 imm: -24
		asm.Add.Imm(asm.R2, -24),
		// 38:	callx r3, Call FnMapDeleteElem
		asm.FnMapDeleteElem.Call(),
		// 39:	*(u64 *)(r10 - 32) = r0, StXMemDW dst: rfp src: r0 off: -32 imm: 0
		asm.StoreMem(asm.RFP, -32, asm.R0, asm.DWord),
		// 40:	r1 = *(u64 *)(r10 - 32), LdXMemDW dst: r1 src: rfp off: -32 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -32, asm.DWord),
		// 41:	if r1 == 0 goto +40 <LBB0_10>, JEqImm dst: r1 off: 40 imm: 0
		asm.JEq.Imm(asm.R1, 0, "LBB0_10"),
		// 42:	goto +0 <LBB0_5>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_5"),
		// 43:	r1 = 8 ll, LoadMapValue dst: r1, fd: 0 off: 8 <.data>
		asm.LoadMapValue(asm.R1, 0, 8).WithSymbol("LBB0_5").WithReference(".rodata"),
		// 45:	*(u64 *)(r10 - 56) = r1, StXMemDW dst: rfp src: r1 off: -56 imm: 0
		asm.StoreMem(asm.RFP, -56, asm.R1, asm.DWord),
		// 46:	r4 = *(u64 *)(r1 + 0), LdXMemDW dst: r4 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R4, asm.R1, 0, asm.DWord),
		// 47:	r1 = *(u64 *)(r10 - 16), LdXMemDW dst: r1 src: rfp off: -16 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -16, asm.DWord),
		// 48:	r2 = 0 ll, LoadMapPtr dst: r2 fd: 0 <xm_syscalls_stack_map>
		asm.LoadMapPtr(asm.R2, 0).WithReference("xm_syscalls_stack_map"),
		// 50:	*(u64 *)(r10 - 64) = r2, StXMemDW dst: rfp src: r2 off: -64 imm: 0
		asm.StoreMem(asm.RFP, -64, asm.R2, asm.DWord),
		// 51:	r3 = 512, MovImm dst: r3 imm: 512
		asm.Mov.Imm(asm.R3, 512),
		// 52:	callx r4, Call FnProbeRead
		asm.FnProbeRead.Call(),
		// 53:	r2 = *(u64 *)(r10 - 64), LdXMemDW dst: r2 src: rfp off: -64 imm: 0
		asm.LoadMem(asm.R2, asm.RFP, -64, asm.DWord),
		// 54:	r1 = *(u64 *)(r10 - 56), LdXMemDW dst: r1 src: rfp off: -56 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -56, asm.DWord),
		// 55:	*(u32 *)(r10 - 36) = r0, StXMemW dst: rfp src: r0 off: -36 imm: 0
		asm.StoreMem(asm.RFP, -36, asm.R0, asm.Word),
		// 56:	r4 = *(u64 *)(r1 + 0), LdXMemDW dst: r4 src: r1 off: 0 imm: 0
		asm.LoadMem(asm.R4, asm.R1, 0, asm.DWord),
		// 57:	r1 = *(u64 *)(r10 - 16), LdXMemDW dst: r1 src: rfp off: -16 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -16, asm.DWord),
		// 58:	r3 = 768, MovImm dst: r3 imm: 768
		asm.Mov.Imm(asm.R3, 768),
		// 59:	callx r4, Call FnProbeRead
		asm.FnProbeRead.Call(),
		// 60:	*(u32 *)(r10 - 40) = r0, StXMemW dst: rfp src: r0 off: -40 imm: 0
		asm.StoreMem(asm.RFP, -40, asm.R0, asm.Word),
		// 61:	r2 = *(u32 *)(r10 - 36), LdXMemW dst: r2 src: rfp off: -36 imm: 0
		asm.LoadMem(asm.R2, asm.RFP, -36, asm.Word),
		// 62:	r2 <<= 32, LShImm dst: r2 imm: 32
		asm.LSh.Imm(asm.R2, 32),
		// 63:	r2 s>>= 32, ArShImm dst: r2 imm: 32
		asm.ArSh.Imm(asm.R2, 32),
		// 64:	r1 = 1, MovImm dst: r1 imm: 1
		asm.Mov.Imm(asm.R1, 1),
		// 65:	if r1 s> r2 goto +5 <LBB0_7>, JSGTReg dst: r1 off: 5 src: r2
		asm.JSGT.Reg(asm.R1, asm.R2, "LBB0_7"),
		// 66:	goto +0 <LBB0_6>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_6"),
		// 67:	r1 = *(u32 *)(r10 - 36), LdXMemW dst: r1 src: rfp off: -36 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -36, asm.Word).WithSymbol("LBB0_6"),
		// 68:	r2 = *(u64 *)(r10 - 32), LdXMemDW dst: r2 src: rfp off: -32 imm: 0
		asm.LoadMem(asm.R2, asm.RFP, -32, asm.DWord),
		// 69:	*(u32 *)(r2 + 40) = r1, StXMemW dst: r2 src: r1 off: 40 imm: 0
		asm.StoreMem(asm.R2, 40, asm.R1, asm.Word),
		// 70:	goto +0 <LBB0_7>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_7"),
		// 71:	r2 = *(u32 *)(r10 - 40), LdXMemW dst: r2 src: rfp off: -40 imm: 0
		asm.LoadMem(asm.R2, asm.RFP, -40, asm.Word).WithSymbol("LBB0_7"),
		// 72:	r2 <<= 32, LShImm dst: r2 imm: 32
		asm.LSh.Imm(asm.R2, 32),
		// 73:	r2 s>>= 32, ArShImm dst: r2 imm: 32
		asm.ArSh.Imm(asm.R2, 32),
		// 74:	r1 = 1, MovImm dst: r1 imm: 1
		asm.Mov.Imm(asm.R1, 1),
		// 75:	if r1 s> r2 goto +5 <LBB0_9>, JSGTReg dst: r1 off: 5 src: r2
		asm.JSGT.Reg(asm.R1, asm.R2, "LBB0_9"),
		// 76:	goto +0 <LBB0_8>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_8"),
		// 77:	r1 = *(u32 *)(r10 - 40), LdXMemW dst: r1 src: rfp off: -40 imm: 0
		asm.LoadMem(asm.R1, asm.RFP, -40, asm.Word).WithSymbol("LBB0_8"),
		// 78:	r2 = *(u64 *)(r10 - 32), LdXMemDW dst: r2 src: rfp off: -32 imm: 0
		asm.LoadMem(asm.R2, asm.RFP, -32, asm.DWord),
		// 79:	*(u32 *)(r2 + 44) = r1, StXMemW dst: r2 src: r1 off: 44 imm: 0
		asm.StoreMem(asm.R2, 44, asm.R1, asm.Word),
		// 80:	goto +0 <LBB0_9>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_9"),
		// 81:	goto +0 <LBB0_10>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_10").WithSymbol("LBB0_9"),
		// 82:	r1 = 0, MovImm dst: r1 imm: 0
		asm.Mov.Imm(asm.R1, 0).WithSymbol("LBB0_10"),
		// 83:	*(u32 *)(r10 - 4) = r1, StXMemW dst: rfp src: r1 off: -4 imm: 0
		asm.StoreMem(asm.RFP, -4, asm.R1, asm.Word),
		// 84:	goto +0 <LBB0_11>, JaImm dst: r0 off: 0 imm: 0
		asm.Ja.Label("LBB0_11"),
		// 85:	r0 = *(u32 *)(r10 - 4), LdXMemW dst: r0 src: rfp off: -4 imm: 0
		asm.LoadMem(asm.R0, asm.RFP, -4, asm.Word).WithSymbol("LBB0_11"),
		// 86:	exit, Exit
		asm.Return(),
	}

	glog.Infof("\n%s", insns.String())
}

func main() {
	goflag.Parse()

	glog.Infof("elf file:'%s'", __elfFile)
	glog.Infof("btf file:'%s'", __btfFile)

	// Load the elf file
	spec, err := ebpf.LoadCollectionSpec(__elfFile)
	if err != nil {
		glog.Fatalf("failed to load collection spec from file: %v", err)
	}

	for key, progSpec := range spec.Programs {
		glog.Infof(`progSpec key:'%s', Name:'%s', ProgramType:%d, AttachType:%d, AttachTo:'%s',
		SectionName:'%s', AttachTarget:%p, Instructions len:%d`,
			key, progSpec.Name, progSpec.Type, progSpec.AttachType,
			progSpec.AttachTo, progSpec.SectionName, progSpec.AttachTarget, len(progSpec.Instructions))

		if progSpec.Type == ebpf.Tracing {
			glog.Info("\t", progSpec.Instructions.String())
		}
	}

	var objs struct {
		Prog *ebpf.Program `ebpf:"xm_trace_tp_btf__sys_enter"`
	}

	// 加载 reduce btf file
	btfSpec, err := btf.LoadSpec(__btfFile)
	if err != nil {
		glog.Fatalf("failed to load btf spec from file: %v", err)
	}

	err = spec.LoadAndAssign(&objs, &ebpf.CollectionOptions{Programs: ebpf.ProgramOptions{
		LogLevel:    ebpf.LogLevelInstruction,
		KernelTypes: btfSpec,
	}})

	if err != nil {
		glog.Fatalf("failed to load and assign programs: %v", err)
	}

	defer objs.Prog.Close()
	// VerifierLog 是验证器的日志，用于调试
	glog.Infof("objs.Prog:'%s' load VerifierLog:%s", objs.Prog.String(), objs.Prog.VerifierLog)

	// 输出指令
	// for i, ins := range firstProgSpec.Instructions {
	// 	glog.Infof("%d:\t%#v", i, ins)
	// }

	//__generate_asm_insns()

	glog.Infof("parse elf file:'%s' exit", __elfFile)
}

// llvm-objdump -d -S --no-show-raw-insn --symbolize-operands xm_trace.bpf.o
// ./xm_progspec --elf=../plugin_ebpf/bpf/.output/xm_trace.bpf.o --alsologtostderr -v=4
/*
I0806 10:31:14.550911  122570 main.go:202] progSpec key:'xm_trace_tp_btf__sys_exit', Name:'xm_trace_tp_btf__sys_exit', ProgramType:26, AttachType:23, AttachTo:'sys_exit',
		SectionName:'tp_btf/sys_exit', AttachTarget:0x0, Instructions len:67
I0806 10:31:14.551310  122570 main.go:202] progSpec key:'xm_trace_tp_btf__sys_enter', Name:'xm_trace_tp_btf__sys_enter', ProgramType:26, AttachType:23, AttachTo:'sys_enter',
		SectionName:'tp_btf/sys_enter', AttachTarget:0x0, Instructions len:34

[5885] TYPEDEF 'btf_trace_sys_enter' type_id=5886
[5886] PTR '(anon)' type_id=5887
[5887] FUNC_PROTO '(anon)' ret_type_id=0 vlen=3
	'(anon)' type_id=109
	'(anon)' type_id=381
	'(anon)' type_id=42

*/
