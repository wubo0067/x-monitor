/*
 * @Author: CALM.WU
 * @Date: 2024-08-13 14:04:20
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-13 14:47:49
 */

// **用来在 uprobe 中获取 golang 函数参数
// https://tip.golang.org/src/cmd/compile/abi-internal
/*
amd64 architecture¶
The amd64 architecture uses the following sequence of 9 registers for integer
arguments and results:

RAX, RBX, RCX, RDI, RSI, R8, R9, R10, R11

arm64 architecture¶
The arm64 architecture uses R0 – R15 for integer arguments and results.
It uses F0 – F15 for floating-point arguments and results.
*/

#pragma once

#if defined(__TARGET_ARCH_x86)
#define GOLANG_PARAM1(x) BPF_CORE_READ((x), ax)
#define GOLANG_PARAM2(x) BPF_CORE_READ((x), bx)
#define GOLANG_PARAM3(x) BPF_CORE_READ((x), cx)
#define GOLANG_PARAM4(x) BPF_CORE_READ((x), di)
#define GOLANG_PARAM5(x) BPF_CORE_READ((x), si)
#define GOLANG_PARAM6(x) BPF_CORE_READ((x), r8)
#define GOLANG_PARAM7(x) BPF_CORE_READ((x), r9)
#define GOLANG_PARAM8(x) BPF_CORE_READ((x), r10)
#define GOLANG_PARAM9(x) BPF_CORE_READ((x), r11)
#define GOLANG_GOROUTINE(x) BPF_CORE_READ((x), r14)
#define GOLANG_SP(x) BPF_CORE_READ((x), sp)
#elif defined(__TARGET_ARCH_arm64)
#define GOLANG_PARAM1(x) BPF_CORE_READ((x), regs[0])
#define GOLANG_PARAM2(x) BPF_CORE_READ((x), regs[1])
#define GOLANG_PARAM3(x) BPF_CORE_READ((x), regs[2])
#define GOLANG_PARAM4(x) BPF_CORE_READ((x), regs[3])
#define GOLANG_PARAM5(x) BPF_CORE_READ((x), regs[4])
#define GOLANG_PARAM6(x) BPF_CORE_READ((x), regs[5])
#define GOLANG_PARAM7(x) BPF_CORE_READ((x), regs[6])
#define GOLANG_PARAM8(x) BPF_CORE_READ((x), regs[7])
#define GOLANG_PARAM9(x) BPF_CORE_READ((x), regs[8])
#define GOLANG_GOROUTINE(x) BPF_CORE_READ((x), regs[22])
#define GOLANG_SP(x) BPF_CORE_READ((x), sp)
#else
#error "Unsupported architecture"
#endif

// 通过寄存器获取 golang 函数参数
static __always_inline void *__xm_get_golang_arg_by_reg(struct pt_regs *ctx,
							int32_t index)
{
	switch (index) {
	case 1:
		return (void *)GOLANG_PARAM1(ctx);
	case 2:
		return (void *)GOLANG_PARAM2(ctx);
	case 3:
		return (void *)GOLANG_PARAM3(ctx);
	case 4:
		return (void *)GOLANG_PARAM4(ctx);
	case 5:
		return (void *)GOLANG_PARAM5(ctx);
	case 6:
		return (void *)GOLANG_PARAM6(ctx);
	case 7:
		return (void *)GOLANG_PARAM7(ctx);
	case 8:
		return (void *)GOLANG_PARAM8(ctx);
	case 9:
		return (void *)GOLANG_PARAM9(ctx);
	default:
		return NULL;
	}
}

// 通过栈获取 golang 函数参数
static __always_inline void *__xm_get_golang_arg_by_stack(struct pt_regs *ctx,
							  int32_t index)
{
	// 从 frame 的 sp 寄存器做偏移读取参数，记住 rbp 在顶部，rsp 在底部，所以 rsp 需要向上偏移
	void *p = NULL;
	bpf_probe_read(&p, sizeof(void *),
		       (void *)(GOLANG_SP(ctx) + index * sizeof(void *)));
	return p;
}

// 获取 golang 函数参数
static void *__xm_get_golang_arg(struct pt_regs *ctx, bool is_reg_abi,
				 int32_t index)
{
	if (is_reg_abi) {
		return __xm_get_golang_arg_by_reg(ctx, index);
	}
	return __xm_get_golang_arg_by_stack(ctx, index);
}