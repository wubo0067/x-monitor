# Makefile for latencytop
# Author: CalmWu
# Date: 2025年6月25日

# 程序名称
PROGRAM = latencytop

# 输出目录
OUTDIR = ../bin

# 编译器和标志
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =
LIBS =

# 源文件和目标文件
SOURCES = latencytop.c
OBJECTS = $(SOURCES:.c=.o)

# 完整的程序路径
PROGRAM_PATH = $(OUTDIR)/$(PROGRAM)

# 安装路径
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

# 默认目标
.PHONY: all clean install uninstall help

all: $(PROGRAM_PATH)

# 链接可执行文件，在构建目标文件之前，OUTDIR 必须存在，| 符号后面的是"仅顺序依赖"
$(PROGRAM_PATH): $(OBJECTS) | $(OUTDIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# 编译目标文件
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理生成的文件
clean:
	rm -f $(OBJECTS) $(PROGRAM)

# 安装程序
install: $(PROGRAM)
	@echo "Installing $(PROGRAM) to $(BINDIR)..."
	install -d $(BINDIR)
	install -m 755 $(PROGRAM) $(BINDIR)/
	@echo "Installation complete."

# 卸载程序
uninstall:
	@echo "Uninstalling $(PROGRAM) from $(BINDIR)..."
	rm -f $(BINDIR)/$(PROGRAM)
	@echo "Uninstallation complete."

# 调试版本
debug: CFLAGS += -DDEBUG -O0
debug: clean $(PROGRAM)

# 发布版本
release: CFLAGS += -DNDEBUG -O3
release: clean $(PROGRAM)

# 静态链接版本
static: LDFLAGS += -static
static: $(PROGRAM)

# 检查代码风格 (需要安装 cppcheck)
check:
	@which cppcheck > /dev/null || (echo "cppcheck not found, skipping..." && exit 0)
	cppcheck --enable=all --std=c99 --platform=unix64 $(SOURCES)

# 格式化代码 (需要安装 clang-format)
format:
	@which clang-format > /dev/null || (echo "clang-format not found, skipping..." && exit 0)
	clang-format -i $(SOURCES)

# 创建源码包
dist: clean
	@echo "Creating distribution package..."
	tar -czf $(PROGRAM)-$(shell date +%Y%m%d).tar.gz \
	    $(SOURCES) Makefile README* LICENSE* ChangeLog* || \
	    tar -czf $(PROGRAM)-$(shell date +%Y%m%d).tar.gz $(SOURCES) Makefile

# 显示帮助信息
help:
	@echo "Available targets:"
	@echo "  all       - Build the program (default)"
	@echo "  clean     - Remove generated files"
	@echo "  install   - Install to $(BINDIR)"
	@echo "  uninstall - Remove from $(BINDIR)"
	@echo "  debug     - Build debug version"
	@echo "  release   - Build optimized release version"
	@echo "  static    - Build static linked version"
	@echo "  check     - Run static code analysis"
	@echo "  format    - Format source code"
	@echo "  dist      - Create distribution package"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  PREFIX=$(PREFIX)"

# 依赖关系
latencytop.o: latencytop.c

# 让 make 知道这些不是文件名
.PHONY: all clean install uninstall debug release static check format dist help