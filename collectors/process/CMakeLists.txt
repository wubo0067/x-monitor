# 目录下所有的源代码文件并将列表存储在XMONITOR_PROCESS_STAT_SRCS中
AUX_SOURCE_DIRECTORY(. XMONITOR_PROCESS_STAT_SRCS)

#会追加到父CMakeLists.txt的include变量中
#include_directories("${EXTRA_DIR}/include")

#设置静态库输出目录                   
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/libs)
add_library(xmonitor-process-stat ${XMONITOR_PROCESS_STAT_SRCS})