/*
 * @Author: CALM.WU
 * @Date: 2021-10-22 11:57:56
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-02-11 14:44:34
 */

#pragma once

#define XM_PID_FILENAME_MAX 32
#define XM_NUMBER_BUFFER_SIZE 32
#define XM_FILENAME_MAX 512
#define XM_CONFIG_NAME_MAX 128
#define XM_STDOUT_LINE_BUF_SIZE 1024
#define XM_DEV_NAME_MAX 128
#define XM_MAC_BUF_SIZE 18
#define XM_IP_BUF_SIZE 16
#define XM_PROM_METRIC_NAME_LEN 32
#define XM_PPROM_METRIC_LABEL_VALUE_LEN 128
#define XM_ITF_NAME_SIZE 64
#define XM_CMD_LINE_MAX 1024

extern char *DEFAULT_PIDFILE;

extern char premetheus_instance_label[XM_PPROM_METRIC_LABEL_VALUE_LEN];