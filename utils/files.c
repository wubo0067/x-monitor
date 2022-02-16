/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 16:55:25
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-22 12:03:32
 */

#include "compiler.h"
#include "consts.h"
#include "files.h"
#include "log.h"

// 返回读取的字节数
static int32_t read_file(const char *file_name, char *buffer, size_t buffer_size) {
    if (unlikely(0 == buffer_size)) {
        return -EINVAL;
    }

    int32_t fd = open(file_name, O_RDONLY, 0666);
    if (unlikely(-1 == fd)) {
        error("open file %s failed, reason: %s", file_name, strerror(errno));
        return fd;
    }

    ssize_t read_size = read(fd, buffer, buffer_size);
    if (unlikely(-1 == read_size)) {
        error("read file %s failed, reason: %s", file_name, strerror(errno));
        close(fd);
        return -1;
    }

    buffer[ read_size ] = '\0';
    return ( int32_t )read_size;
}

int32_t read_file_to_int64(const char *file_name, int64_t *number) {
    char number_buffer[ NUMBER_BUFFER_SIZE + 1 ] = { 0 };

    if (unlikely(NULL == file_name || NULL == number)) {
        return -EINVAL;
    }

    int32_t ret = read_file(file_name, number_buffer, NUMBER_BUFFER_SIZE);
    if (unlikely(ret <= 0)) {
        *number = 0;
        return ret;
    }

    char *endptr = NULL;
    *number      = strtoll(number_buffer, &endptr, 10);
    return 0;
}

int32_t write_int64_to_file(const char *file_name, int64_t number) {
    if (unlikely(NULL == file_name)) {
        return -EINVAL;
    }

    ssize_t ret = 0;

    int32_t fd = open(file_name, O_WRONLY, 0666);
    if (likely(-1 != fd)) {
        char number_buffer[ NUMBER_BUFFER_SIZE + 1 ] = { 0 };
        snprintf(number_buffer, NUMBER_BUFFER_SIZE, "%ld", number);
        ret = write(fd, number_buffer, strlen(number_buffer));
        if (unlikely(-1 == ret)) {
            error("write file %s failed, reason: %s", file_name, strerror(errno));
            close(fd);
            return (0 - errno);
        }
        close(fd);
    }
    return ( int32_t )ret;
}