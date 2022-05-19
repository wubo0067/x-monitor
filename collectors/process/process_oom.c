/*
 * @Author: CALM.WU
 * @Date: 2022-05-19 16:01:15
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-05-19 17:53:57
 */

#include "process_status.h"

#include "utils/common.h"
#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/files.h"

int32_t collector_process_oom(struct process_status *ps) {
    int64_t oom_score, oom_score_adj;
    int32_t ret = 0;

    ret = read_file_to_int64(ps->oom_score_full_filename, &oom_score);
    if (unlikely(ret < 0)) {
        error("[PROCESS:oom] open '%s' failed, errno: %d", ps->oom_score_full_filename, errno);
        return -1;
    }

    ret = read_file_to_int64(ps->oom_score_adj_full_filename, &oom_score_adj);
    if (unlikely(ret < 0)) {
        error("[PROCESS:oom] open '%s' failed, errno: %d", ps->oom_score_adj_full_filename, errno);
        return -1;
    }

    ps->oom_score = (int16_t)oom_score;
    ps->oom_score_adj = (int16_t)oom_score_adj;

    debug("[PROCESS:oom] pid: %d, oom_score: %d, oom_score_adj: %d", ps->pid, ps->oom_score,
          ps->oom_score_adj);
    return 0;
}