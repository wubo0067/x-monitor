#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <asm/unistd.h>
#include <sys/mman.h>
#include <numaif.h>

#define PAGESIZE        4096

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
              pid_t pid, int cpu, int group_fd,
              unsigned long flags);

char* mmap_and_bind(size_t size, int nodeid) {
    unsigned long nodemask = 1 << nodeid;
    char* ptr = (char*) mmap(NULL, size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON , -1, 0);

    int j, ret;

    if(ptr == ((char*) -1)) {
        perror("mmap");
        exit(1);
    }

    ret = mbind(ptr, size, MPOL_BIND, &nodemask,
                32, MPOL_MF_MOVE);
    if(ret == -1) {
        perror("mbind");
        exit(1);
    }


    /* TOUCH memory so it really will be allocated */
    for(j = 0; j < size; j += PAGESIZE) {
        ptr[j] = 'X';
    }

    return ptr;
}

int main() {
    size_t size = 16384;

    char* local_ptr;
    char* remote_ptr;

    struct perf_event_mmap_page* pc;
    int perf_fd;

    char* p;
    volatile char c;
    int j;
    long counter;

    struct perf_event_attr attr = {
        .type = PERF_TYPE_HW_CACHE,
        .config = PERF_COUNT_HW_CACHE_NODE |
                (PERF_COUNT_HW_CACHE_OP_READ        <<  8) |
                (PERF_COUNT_HW_CACHE_RESULT_MISS    << 16),
        .exclude_kernel = 1
    };

    local_ptr  = mmap_and_bind(size, 0);
    remote_ptr = mmap_and_bind(size, 1);

    perf_fd = sys_perf_event_open(&attr, gettid(), -1, -1, 0);
    if(perf_fd == -1) {
        perror("perfopen");
        exit(1);
    }

    pc = (struct perf_event_mmap_page*) mmap(NULL, PAGESIZE,
                                             PROT_READ, MAP_SHARED, perf_fd, 0);
    if(pc == NULL) {
        perror("perfmmap");
        exit(1);
    }

    counter = (long) read_counter(pc);
    for(j = 0, p = local_ptr; j < size; ++j, ++p) {
        c = *p;
    }
    printf("Local: %ld\n", read_counter(pc) - counter);

    counter = (long) read_counter(pc);
    for(j = 0, p = remote_ptr; j < size; ++j, ++p) {
        c = *p;
    }
    printf("Remote: %ld\n", read_counter(pc) - counter);

    return 0;
}

static inline int
sys_perf_event_open(struct perf_event_attr *attr,
              pid_t pid, int cpu, int group_fd,
              unsigned long flags)
{
    int fd;

    fd = syscall(__NR_perf_event_open, attr, pid, cpu,
             group_fd, flags);

    return fd;
}


static uint64_t rdpmc(unsigned int counter)
{
    unsigned int low, high;

    asm volatile("rdpmc" : "=a" (low), "=d" (high) : "c" (counter));

    return low | ((uint64_t)high) << 32;
}

// 它用于确保内存操作的顺序性和可见性。它的作用是阻止编译器对内存操作进行重排和优化。
// 可见性：barrier() 保证内存操作的可见性，即保证一个内存操作的结果能够在其他线程或进程中被看到。
#define barrier() asm volatile("" ::: "memory")

static uint64_t read_counter(struct perf_event_mmap_page * pc) {
    uint32_t seq;
    uint32_t idx;
    uint64_t count;

    do {
        seq = pc->lock;
        barrier();

        idx = pc->index;
        count = pc->offset;
        if (idx)
            count += rdpmc(idx - 1);

        barrier();
    } while (pc->lock != seq);

    return count;
}
@wubo0067
Comment
