/*
 * @Author: CALM.WU
 * @Date: 2021-10-15 16:55:32
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-10-15 17:24:02
 */

#pragma once

#include "common.h"

extern int32_t read_file_to_int64(const char *file_name, int64_t *number);
extern int32_t write_int64_to_file(const char *file_name, int64_t number);
