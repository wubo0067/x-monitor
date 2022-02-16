cmake_minimum_required (VERSION 3.18)
project (thread_cancel) 

AUX_SOURCE_DIRECTORY(. thread_cancel_srcs)
add_executable(thread_cancel ${thread_cancel_srcs})
target_link_libraries(thread_cancel pthread z m)