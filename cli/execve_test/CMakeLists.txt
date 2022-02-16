cmake_minimum_required (VERSION 3.18)
project (execve_test) 

AUX_SOURCE_DIRECTORY(. srcs)
add_executable(execve_test ${srcs})
target_link_libraries(execve_test pthread z m)