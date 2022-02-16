/*
 * @Author: CALM.WU
 * @Date: 2021-11-30 14:59:07
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-10 17:08:48
 */

#include "plugin_proc.h"

#include "utils/compiler.h"
#include "utils/consts.h"
#include "utils/log.h"
#include "utils/procfile.h"
#include "utils/strings.h"

#include "appconfig/appconfig.h"

static const char *      __proc_diskstat_filename = "/proc/diskstats";
static struct proc_file *__pf_diskstats           = NULL;

struct io_stats {
    /* 读取的扇区数量 */
    uint64_t rd_sectors;
    /* 写入的扇区数量 */
    uint64_t wr_sectors;
    /* # of sectors discarded */
    uint64_t dc_sectors;
    /* (rd_ios)读操作的次数 */
    uint64_t rd_ios;
    /* 合并读操作的次数。如果两个读操作读取相邻的数据块时，可以被合并成一个，以提高效率。合并的操作通常是I/O
     * scheduler（也叫elevator）负责的 */
    uint64_t rd_merges;
    /* 写操作的次数 */
    uint64_t wr_ios;
    /* 合并写操作的次数 */
    uint64_t wr_merges;
    /* # of discard operations issued to the device */
    uint64_t dc_ios;
    /* # of discard requests merged */
    uint64_t dc_merges;
    /* # of flush requests issued to the device */
    uint64_t fl_ios;
    /* 读操作消耗的时间（以毫秒为单位）。每个读操作从__make_request()开始计时，到end_that_request_last()为止，包括了在队列中等待的时间
     */
    uint64_t rd_ticks;
    /* 写操作消耗的时间（以毫秒为单位） */
    uint64_t wr_ticks;
    /* Time of discard requests in queue */
    uint64_t dc_ticks;
    /* Time of flush requests in queue */
    uint64_t fl_ticks;
    /* # of I/Os in progress
    当前正在进行的I/O数量,唯一应该归零的字段。随着请求的增加而增加,提供给相应的结构请求队列，并在完成时递减。*/
    uint64_t ios_pgr;
    /* 执行I/O所花费的毫秒数，只要排队的字段不为零，此字段就会增加。 */
    uint64_t tot_ticks;
    /* # of ticks requests spent in queue
    number of milliseconds spent doing I/Os. This field is incremented at each I/O start,
    I/O completion, I/O merge, or read of these stats by the number of I/Os in progress (field 9)
    times the number of milliseconds spent doing I/O since the last update of this field.
    This can provide an easy measure of both I/O completion time and the backlog that may be
    accumulating.
    */
    // The expected duration of the currently queued I/O operations
    uint64_t rq_ticks;
};

struct io_device {
    char device_name[MAX_NAME_LEN + 1];  //

    uint32_t       major;
    uint32_t       minor;
    uint32_t       device_hash;
    enum disk_type dev_tp;

    struct io_stats stats[2];
    uint32_t        curr_stats_id;
    uint32_t        prev_stats_id;

    struct io_device *next;
};

//
static const int32_t __kernel_sector_size = 512;
static const double  __kernel_sector_kb   = (double)__kernel_sector_size / 1024.0;
//
static struct io_device *__iodev_list = NULL;

static enum disk_type __get_device_type(const char *dev_name, uint32_t major, uint32_t minor) {
    enum disk_type dt = DISK_TYPE_UNKNOWN;

    char buffer[MAX_NAME_LEN + 1];
    snprintf(buffer, MAX_NAME_LEN, "/sys/block/%s", dev_name);
    if (likely(access(buffer, R_OK) == 0)) {
        // assign it here, but it will be overwritten if it is not a physical disk
        dt = DISK_TYPE_PHYSICAL;
    } else {
        snprintf(buffer, MAX_NAME_LEN, "/sys/dev/block/%u:%u/partition", major, minor);
        if (likely(access(buffer, R_OK) == 0)) {
            dt = DISK_TYPE_PARTITION;
        } else {
            snprintf(buffer, FILENAME_MAX, "/sys/dev/block/%u:%u/slaves", major, minor);
            DIR *dirp = opendir(buffer);
            if (likely(dirp != NULL)) {
                struct dirent *dp;
                while ((dp = readdir(dirp))) {
                    // . and .. are also files in empty folders.
                    if (unlikely(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)) {
                        continue;
                    }

                    dt = DISK_TYPE_VIRTUAL;
                    // Stop the loop after we found one file.
                    break;
                }
                if (unlikely(closedir(dirp) == -1))
                    error("Unable to close dir %s", buffer);
            }
        }
    }

    return dt;
}

static struct io_device *__get_device(char *device_name, uint32_t major, uint32_t minor) {
    struct io_device *dev = NULL;

    // 计算hash值
    uint32_t hash = bkrd_hash(device_name, strlen(device_name));

    // 查找磁盘设备，看是否已经存在
    for (dev = __iodev_list; dev != NULL; dev = dev->next) {
        if (dev->major == major && dev->minor == minor && dev->device_hash == hash) {
            return dev;
        }
    }

    dev = (struct io_device *)calloc(1, sizeof(struct io_device));
    if (unlikely(!dev)) {
        exit(-1);
    }

    strncpy(dev->device_name, device_name, MAX_NAME_LEN);
    dev->major         = major;
    dev->minor         = minor;
    dev->device_hash   = hash;
    dev->dev_tp        = __get_device_type(device_name, major, minor);
    dev->curr_stats_id = dev->prev_stats_id = 0;

    if (unlikely(!__iodev_list)) {
        __iodev_list = dev;
    } else {
        // 添加在末尾
        struct io_device *last;
        for (last = __iodev_list; last->next != NULL; last = last->next)
            ;
        last->next = dev;
    }

    return dev;
}

int32_t init_collector_proc_diskstats() {
    return 0;
}

int32_t collector_proc_diskstats(int32_t UNUSED(update_every), usec_t dt, const char *config_path) {
    debug("[PLUGIN_PROC:proc_diskstats] config:%s running", config_path);

    const char *f_diskstat =
        appconfig_get_member_str(config_path, "monitor_file", __proc_diskstat_filename);

    if (unlikely(!__pf_diskstats)) {
        __pf_diskstats = procfile_open(f_diskstat, " \t", PROCFILE_FLAG_DEFAULT);
        if (unlikely(!__pf_diskstats)) {
            error("Cannot open /proc/diskstats");
            return -1;
        }
    }

    __pf_diskstats = procfile_readall(__pf_diskstats);
    if (unlikely(!__pf_diskstats)) {
        error("Cannot read /proc/diskstats");
        return -1;
    }

    size_t pf_diskstats_lines = procfile_lines(__pf_diskstats), l;

    int64_t system_read_kb = 0, system_write_kb = 0;

    for (l = 0; l < pf_diskstats_lines; l++) {
        // 每一行都是一个磁盘设备
        char *dev_name;
        // 设备的主id，辅id
        uint32_t major, minor;

        size_t pf_diskstats_line_words = procfile_linewords(__pf_diskstats, l);
        if (unlikely(pf_diskstats_line_words < 14)) {
            // error("Cannot read /proc/diskstats: line %zu is too short.", l);
            continue;
        }

        major    = str2uint32_t(procfile_lineword(__pf_diskstats, l, 0));
        minor    = str2uint32_t(procfile_lineword(__pf_diskstats, l, 1));
        dev_name = procfile_lineword(__pf_diskstats, l, 2);

        struct io_device *dev           = __get_device(dev_name, major, minor);
        struct io_stats * curr_devstats = &dev->stats[dev->curr_stats_id];

        curr_devstats->rd_ios = str2uint64_t(procfile_lineword(__pf_diskstats, l, 3));
        curr_devstats->wr_ios = str2uint64_t(procfile_lineword(__pf_diskstats, l, 7));

        curr_devstats->rd_merges = str2uint64_t(procfile_lineword(__pf_diskstats, l, 4));
        curr_devstats->wr_merges = str2uint64_t(procfile_lineword(__pf_diskstats, l, 8));

        curr_devstats->rd_sectors = str2uint64_t(procfile_lineword(__pf_diskstats, l, 5));
        curr_devstats->wr_sectors = str2uint64_t(procfile_lineword(__pf_diskstats, l, 9));

        curr_devstats->rd_ticks = str2uint64_t(procfile_lineword(__pf_diskstats, l, 6));
        curr_devstats->wr_ticks = str2uint64_t(procfile_lineword(__pf_diskstats, l, 10));

        curr_devstats->ios_pgr = str2uint64_t(procfile_lineword(__pf_diskstats, l, 11));

        curr_devstats->tot_ticks = str2uint64_t(procfile_lineword(__pf_diskstats, l, 12));

        curr_devstats->rq_ticks = str2uint64_t(procfile_lineword(__pf_diskstats, l, 13));

        if (unlikely(pf_diskstats_line_words > 13)) {
            curr_devstats->dc_ios     = str2uint64_t(procfile_lineword(__pf_diskstats, l, 14));
            curr_devstats->dc_merges  = str2uint64_t(procfile_lineword(__pf_diskstats, l, 15));
            curr_devstats->dc_sectors = str2uint64_t(procfile_lineword(__pf_diskstats, l, 16));
            curr_devstats->dc_ticks   = str2uint64_t(procfile_lineword(__pf_diskstats, l, 17));
        }

        // debug(
        //     "\ndiskstats[%d:%d]: %s rd_ios=%lu, rd_merges=%lu, rd_sectors=%lu, rd_ticks=%lu,
        //     wr_ios=%lu, wr_merges=%lu, wr_sectors=%lu, wr_ticks=%lu, ios_pgr=%lu, tot_ticks=%lu,
        //     rq_ticks=%lu, dc_ios=%lu, dc_merges=%lu, dc_sectors=%lu, dc_ticks=%lu", dev->major,
        //     dev->minor, dev->device_name, curr_devstats->rd_ios, curr_devstats->rd_merges,
        //     curr_devstats->rd_sectors, curr_devstats->rd_ticks, curr_devstats->wr_ios,
        //     curr_devstats->wr_merges, curr_devstats->wr_sectors, curr_devstats->wr_ticks,
        //     curr_devstats->ios_pgr, curr_devstats->tot_ticks, curr_devstats->rq_ticks,
        //     curr_devstats->dc_ios, curr_devstats->dc_merges, curr_devstats->dc_sectors,
        //     curr_devstats->dc_ticks);

        if (unlikely(0 == dev->curr_stats_id && 0 == dev->prev_stats_id)) {
            dev->curr_stats_id = (dev->curr_stats_id + 1) % 2;
            // debug("disk[%d:%d]:'%s' prev_stats_id = %d, curr_stats_id = %d",
            //       major, minor, dev_name, dev->prev_stats_id,
            //       dev->curr_stats_id);
            continue;
        }

        //--------------------------------------------------------------------
        // do performance metrics
        struct io_stats *prev_devstats = &dev->stats[dev->prev_stats_id];

        // 计算两次采集间隔时间，单位秒
        double dt_sec = (double)dt / (double)USEC_PER_SEC;
        debug("[PLUGIN_PROC:proc_diskstats] disk[%d:%d]:'%s' dt_sec=%.2f, dt=%lu", major, minor,
              dev_name, dt_sec, dt);

        // IO每秒读次数
        double rd_ios_per_sec = ((double)(curr_devstats->rd_ios - prev_devstats->rd_ios)) / dt_sec;
        // IO每秒写次数
        double rw_ios_per_sec = ((double)(curr_devstats->wr_ios - prev_devstats->wr_ios)) / dt_sec;

        // 每秒读取字节数
        double rd_kb_per_sec =
            ((double)(curr_devstats->rd_sectors - prev_devstats->rd_sectors)) / 2.0 / dt_sec;
        // 每秒写入字节数
        double wr_kb_per_sec =
            ((double)(curr_devstats->wr_sectors - prev_devstats->wr_sectors)) / 2.0 / dt_sec;

        // 每秒合并读次数 rrqm/s
        double rd_merges_per_sec =
            ((double)(curr_devstats->rd_merges - prev_devstats->rd_merges)) / dt_sec;
        // 每秒合并写次数 wrqm/s
        double wr_merges_per_sec =
            ((double)(curr_devstats->wr_merges - prev_devstats->wr_merges)) / dt_sec;

        // 每个读操作的耗时（毫秒）
        double r_await = (curr_devstats->rd_ios - prev_devstats->rd_ios) ?
                             (curr_devstats->rd_ticks - prev_devstats->rd_ticks)
                                 / ((double)(curr_devstats->rd_ios - prev_devstats->rd_ios)) :
                             0.0;

        // 每个写操作的耗时（毫秒）
        double w_await = (curr_devstats->wr_ios - prev_devstats->wr_ios) ?
                             (curr_devstats->wr_ticks - prev_devstats->wr_ticks)
                                 / ((double)(curr_devstats->wr_ios - prev_devstats->wr_ios)) :
                             0.0;

        uint64_t curr_nr_ios =
            curr_devstats->rd_ios + curr_devstats->wr_ios + curr_devstats->dc_ios;
        uint64_t prev_nr_ios =
            prev_devstats->rd_ios + prev_devstats->wr_ios + prev_devstats->dc_ios;
        // io次数除以时间（毫秒）
        double await = (curr_nr_ios - prev_nr_ios) ?
                           ((curr_devstats->rd_ticks - prev_devstats->rd_ticks)
                            + (curr_devstats->wr_ticks - prev_devstats->wr_ticks)
                            + (curr_devstats->dc_ticks - prev_devstats->dc_ticks))
                               / ((double)(curr_nr_ios - prev_nr_ios)) :
                           0.0;

        // 平均未完成的I/O请求数量，时间单位秒，time_in_queue，time_in_queue是用当前的I/O数量（即字段#9
        // in-flight）乘以自然时间
        double aqu_sz =
            ((double)(curr_devstats->rq_ticks - prev_devstats->rq_ticks)) / dt_sec / 1000.0;

        // 该硬盘设备的繁忙比率
        double utils =
            ((double)(curr_devstats->tot_ticks - prev_devstats->tot_ticks)) / dt_sec / 10.0;
        ;

        // 每个I/O的平均扇区数
        double arq_sz = (curr_nr_ios - prev_nr_ios) ?
                            ((curr_devstats->rd_sectors - prev_devstats->rd_sectors)
                             + (curr_devstats->wr_sectors - prev_devstats->wr_sectors))
                                / ((double)(curr_nr_ios - prev_nr_ios)) / 2.0 :
                            0.0;
        // /* rareq-sz (still in sectors, not kB) */
        double rarq_sz = (curr_devstats->rd_ios - prev_devstats->rd_ios) ?
                             (curr_devstats->rd_sectors - prev_devstats->rd_sectors)
                                 / (double)(curr_devstats->rd_ios - prev_devstats->rd_ios) / 2.0 :
                             0.0;
        double warq_sz = (curr_devstats->wr_ios - prev_devstats->wr_ios) ?
                             (curr_devstats->wr_sectors - prev_devstats->wr_sectors)
                                 / (double)(curr_devstats->wr_ios - prev_devstats->wr_ios) / 2.0 :
                             0.0;

        if (dev->dev_tp == DISK_TYPE_PHYSICAL) {
            // count the global system disk I/O of physical disks
            system_read_kb += curr_devstats->rd_sectors / 2;
            system_write_kb += curr_devstats->wr_sectors / 2;

            debug("[PLUGIN_PROC:proc_diskstats] disk[%d:%d]:'%s' system_read_kb=%ld(Kb ), "
                  "system_write_kb=%ld(Kb), "
                  "rd_ios_per_sec=%.2f(r/s), "
                  "rw_ios_per_sec=%.2f(w/s), "
                  "rd_kb_per_sec=%.2f(rkB/s), wr_kb_per_sec=%.2f(wkB/s), "
                  "rd_merges_per_sec=%.2f(rrqm/s), "
                  "wr_merges_per_sec=%.2f(wrqm/s), r_await=%.2f(ms), w_await=%.2f(ms), "
                  "await=%.2f(ms), "
                  "aqu_sz=%.2f, arq_sz=%.2f, rarq_sz=%.2f, warq_sz=%.2f， utils=%.2f\n",
                  major, minor, dev_name, system_read_kb, system_write_kb, rd_ios_per_sec,
                  rw_ios_per_sec, rd_kb_per_sec, wr_kb_per_sec, rd_merges_per_sec,
                  wr_merges_per_sec, r_await, w_await, await, aqu_sz, arq_sz, rarq_sz, warq_sz,
                  utils);
        } else {
            debug("[PLUGIN_PROC:proc_diskstats] disk[%d:%d]:'%s' rd_ios_per_sec=%.2f(r/s), "
                  "rw_ios_per_sec=%.2f(w/s), "
                  "rd_kb_per_sec=%.2f(rkB/s), wr_kb_per_sec=%.2f(wkB/s), "
                  "rd_merges_per_sec=%.2f(rrqm/s), "
                  "wr_merges_per_sec=%.2f(wrqm/s), r_await=%.2f(ms), w_await=%.2f(ms), "
                  "await=%.2f(ms), "
                  "aqu_sz=%.2f, arq_sz=%.2f, rarq_sz=%.2f, warq_sz=%.2f， utils=%.2f\n",
                  major, minor, dev_name, rd_ios_per_sec, rw_ios_per_sec, rd_kb_per_sec,
                  wr_kb_per_sec, rd_merges_per_sec, wr_merges_per_sec, r_await, w_await, await,
                  aqu_sz, arq_sz, rarq_sz, warq_sz, utils);
        }

        // --------------------------------------------------------------------
        dev->prev_stats_id = dev->curr_stats_id;
        dev->curr_stats_id = (dev->curr_stats_id + 1) % 2;
        // debug("disk[%d:%d]:'%s' prev_stats_id = %d, curr_stats_id = %d",
        //       major, minor, dev_name, dev->prev_stats_id, dev->curr_stats_id);
    }

    return 0;
}

void fini_collector_proc_diskstats() {
}