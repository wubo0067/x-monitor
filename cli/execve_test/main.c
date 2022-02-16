/*
 * @Author: CALM.WU 
 * @Date: 2021-11-05 16:35:31 
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-11-05 17:47:42
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int32_t main(int argc, char const *argv[])
{
    fprintf(stdout, "pid: %d\n", getpid());
    
    char *const exe_argv[] = { "sleep", "30", NULL };
    char *const exe_envp[] = { "PATH=/bin", NULL };

    execve("/usr/bin/sleep", exe_argv, exe_envp);

    fprintf(stdout, "after execve, sleep 60\n");

    sleep(60);

    return 0;
}
