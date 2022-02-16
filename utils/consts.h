/*
 * @Author: CALM.WU
 * @Date: 2021-10-22 11:57:56
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-11 14:44:34
 */

#pragma once

#define PID_FILENAME_MAX 32
#define NUMBER_BUFFER_SIZE 32
#define EXTERNAL_PLUGIN_CMD_LINE_MAX (FILENAME_MAX * 2)
#define CONFIG_NAME_MAX 1024
#define STDOUT_LINE_BUF_SIZE 1024
#define MAX_NAME_LEN 128
#define MAC_BUF_SIZE 18
#define IP_BUF_SIZE 16
#define PROM_METRIC_NAME_LEN 32
#define PROM_METRIC_LABEL_VALUE_LEN 128
#define ITF_NAME_SIZE 64

extern char *DEFAULT_PIDFILE;

extern char premetheus_instance_label[PROM_METRIC_LABEL_VALUE_LEN];