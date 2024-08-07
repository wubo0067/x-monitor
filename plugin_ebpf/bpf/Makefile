OUTPUT := .output
CLANG ?= clang
LLVM_STRIP	?= llvm-strip #除去调试信息
#BPFTOOL ?= $(abspath ../../extra/libbpf/tools/bpftool)
BPFTOOL ?= $(shell which bpftool)

# 当前目录下所有c代码
SRCS			= $(wildcard *.c)
# 文件名替换，将$(SRCS)中所有符合x.bpf.c的文件 ===> .output/x.bpf.o
BPF_KERN_OBJS  := $(patsubst %.bpf.c, $(OUTPUT)/%.bpf.o, $(SRCS))
BPF_EB_KERN_OBJS := $(patsubst %.bpf.c, $(OUTPUT)/%_eb.bpf.o, $(SRCS))
$(info BPF_EB_KERN_OBJS is $(BPF_EB_KERN_OBJS))
# 获取LIBBPF_SRC的绝对路径
LIBBPF_SRC := $(abspath ../../extra/vendor/libbpf/libbpf_repo/src)
#编译到该目录
LIBBPF_OBJ := $(abspath ../../extra/vendor/libbpf/libbpf_repo/src/libbpf.a)
# bpftool gen skeleton生成文件的绝对路径
BPFTOOL_SKEL_OUTPUT := $(abspath ../skel)
# bpftool gen min_core_btf生成文件的绝对路径
BPFTOOL_BTF_OUTPUT := $(abspath ../btf)
# 文件名替换，将$(SRCS)中所有符合x.bpf.c的文件 ===> x.skel.h
BPF_SEKL_HS = $(patsubst %.bpf.c, %.skel.h, $(SRCS))

# 这段命令会输出 clang 编译器的系统头文件目录
CLANG_BPF_SYS_INCLUDES = $(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')

INCLUDES := -I$(abspath $(OUTPUT)) \
			-I$(abspath ../../extra/include) \
			-I$(abspath ../../extra/vendor/libbpf/libbpf_repo/include/uapi)

CFLAGS := -g -Wall -O2

# 系统架构
ARCH := $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/' | sed 's/ppc64le/powerpc/' | sed 's/mips.*/mips/')

# Try to detect best kernel BTF source
KERNEL_REL := $(shell uname -r)
OS_ID := $(shell grep '^ID=' /etc/os-release | cut -d'=' -f2)
OS_VERSION := $(shell grep '^VERSION_ID=' /etc/os-release | cut -d'=' -f2)
VMLINUX_BTF_PATHS := /sys/kernel/btf/vmlinux /boot/vmlinux-$(KERNEL_REL)
VMLINUX_BTF_PATH := $(or $(VMLINUX_BTF),$(firstword			       \
					  $(wildcard $(VMLINUX_BTF_PATHS))))
BTF_FILE := xm-$(OS_ID)$(OS_VERSION)-$(KERNEL_REL).btf

# 如果 V 变量是通过命令行传递的（即 make V=1），则执行下面的代码
ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif

ifndef VERBOSE
  VERBOSE = 0
endif

ifeq ($(VERBOSE),1)
  Q =
  msg =
else
  Q = @ #执行时不会显示这些命令本身，只显示命令的输出
  msg = @printf '  %-8s %s%s\n' "$(1)" "$(notdir $(2))" "$(if $(3), $(3))"; # 定义了一个格式化输出的命令
  QUIET_CC       = @echo '  CC       '$@;
  QUIET_CC_FPIC  = @echo '  CC FPIC  '$@;
  QUIET_CLANG    = @echo '  CLANG    '$@;
  QUIET_AR       = @echo '  AR       '$@;
  QUIET_LINK     = @echo '  LINK     '$@;
  QUIET_MKDIR    = @echo '  MKDIR    '$@;
  QUIET_GEN      = @echo '  GEN      '$@;
  QUIET_FLEX     = @echo '  FLEX     '$@;
  QUIET_BISON    = @echo '  BISON    '$@;
  QUIET_GENSKEL  = @echo '  GEN-SKEL '$@;
  QUIET_CLEAN    = @printf '  CLEAN    %s\n' $1;
  QUIET_INSTALL  = @printf '  INSTALL  %s\n' $1;
  QUIET_UNINST   = @printf '  UNINST   %s\n' $1;
  submake_extras := feature_display=0
endif

.PHONY: all clean skels
all: $(BPF_KERN_OBJ) skels gen_min_xm_btf $(BPF_EB_KERN_OBJS)

# 静态模式规则,
# 目标模式: $(BPF_KERN_OBJ)
# 目标文件模式: $(OUTPUT)/%.bpf.o, 这指定了目标文件的具体格式。
# 依赖文件: $(OUTPUT)/vmlinux.h $(LIBBPF_OBJ) %.bpf.c
# 模式匹配: 对于 $(BPF_KERN_OBJ) 中的每个文件，Make 会尝试将其匹配到 $(OUTPUT)/%.bpf.o 模式
# 依赖生成: 对于每个匹配成功的目标，Make 会使用相同的 % 值来生成对应的 .bpf.c 源文件名
# 通用依赖: $(OUTPUT)/vmlinux.h 和 $(LIBBPF_OBJ) 是所有目标共有的依赖项。
# $@: $(OUTPUT)/%.bpf.o
# $(filter %.bpf.c,$^)：从所有依赖项中过滤出 %.bpf.c 文件
$(BPF_KERN_OBJS): $(OUTPUT)/%.bpf.o: $(OUTPUT)/vmlinux.h $(LIBBPF_OBJ) %.bpf.c
	$(call msg,BPF,$@)
	$(Q)$(CLANG) $(CFLAGS) -target bpf -D__TARGET_ARCH_$(ARCH) $(INCLUDES) $(CLANG_BPF_SYS_INCLUDES) -c $(filter %.bpf.c,$^) -o $@
	$(Q)$(LLVM_STRIP) -g $@ # strip useless DWARF info

$(BPF_EB_KERN_OBJS): $(OUTPUT)/%_eb.bpf.o: %.bpf.c
	$(info target is $@)
	$(Q)$(CLANG) $(CFLAGS) -target bpfeb -D__TARGET_ARCH_$(ARCH) $(INCLUDES) $(CLANG_BPF_SYS_INCLUDES) -c $< -o $@
	$(Q)$(LLVM_STRIP) -g $@ # strip useless DWARF info

# 两个变量写在一起，$@只是最后一个变量
skels: $(patsubst $(OUTPUT)/%.bpf.o, $(BPFTOOL_SKEL_OUTPUT)/%.skel.h, $(BPF_KERN_OBJ))
	@echo $(BPF_KERN_OBJ)
	$(call msg,GEN-SKELS,$^)

$(BPFTOOL_SKEL_OUTPUT)/%.skel.h: $(OUTPUT)/%.bpf.o
	$(call msg,GEN-SKEL-H,$@)
	$(Q)$(BPFTOOL) gen skeleton $< > $@

$(OUTPUT)/vmlinux.h: $(VMLINUX_BTF_PATH) | $(OUTPUT)
	$(Q)if [ ! -e "$(VMLINUX_BTF_PATH)" ] ; then \
		echo "Couldn't find kernel BTF; set VMLINUX_BTF to"	       \
			"specify its location." >&2;			       \
		exit 1;\
	fi
	$(QUIET_GEN)$(BPFTOOL) btf dump file $(VMLINUX_BTF_PATH) format c > $@

$(LIBBPF_OBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile)
	$(call msg,LIB,$@)
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC) BUILD_STATIC_ONLY=1  \
									OBJDIR=$(dir $@) DESTDIR=	\
									INCLUDEDIR=$(abspath ../../extra/include) LIBDIR=$(abspath ../../extra/libs) UAPIDIR= \
									install

# 为应用程序生成裁剪后的btf文件，这样不需要加载整个vmlinux的btf信息
# 使用skel中LIBBPF_OPTS(bpf_object_open_opts, opts, .btf_custom_path = "/tmp/vmlinux.btf"); xxx_bpf__open_opts(&opts);来加载
gen_min_xm_btf: $(BPF_KERN_OBJS)
	$(call msg,GEN-MIN-XM-BTF,$@)
	$(Q)$(BPFTOOL) gen min_core_btf $(VMLINUX_BTF_PATH) $(BPFTOOL_BTF_OUTPUT)/$(BTF_FILE) $^

$(OUTPUT):
	$(call msg,MKDIR,$@)
	$(Q)mkdir -p $@

clean:
	$(call msg,CLEAN)
	$(Q)rm -rf $(OUTPUT) $(BPF_KERN_OBJ) $(LIBBPF_OBJ) $(BPFTOOL_SKEL_OUTPUT)/*.skel.h ../../../extra/libbpf/staticobjs
