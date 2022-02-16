cmake_minimum_required (VERSION 3.18)
project (thread_cleanup) 

AUX_SOURCE_DIRECTORY(. thread_cleanup_srcs)
add_executable(thread_cleanup ${thread_cleanup_srcs})
target_link_libraries(thread_cleanup pthread z m)