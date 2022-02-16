cmake_minimum_required (VERSION 3.18)
project (readbtf) 

set(CMAKE_C_COMPILER "/usr/bin/gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/g++")

set(KERNEL_DIR "/usr/src/linux-5.12.9")
# 库路径
link_directories("${KERNEL_DIR}/tools/lib/bpf" "../../build/libs")
# 头文件路径
include_directories("/usr/src/linux-5.12.9/usr/include" "../../extra/include" "../.." "/usr/src/linux-5.12.9/tools/lib")

AUX_SOURCE_DIRECTORY(. readbtf_srcs)
add_executable(readbtf ${readbtf_srcs})
target_link_libraries(readbtf libxmonitor-utils.a libbpf.a elf pthread z m)

target_compile_definitions(readbtf PRIVATE -DDEBUG)