/*
 * @Author: CALM.WU
 * @Date: 2021-12-20 16:55:51
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-20 17:07:05
 */

#pragma once

#include <stdint.h>
#include <sys/stat.h>

#define MOUNTINFO_FLAG_IS_DUMMY 0x00000001
#define MOUNTINFO_FLAG_IS_REMOTE 0x00000002
#define MOUNTINFO_FLAG_IS_BIND 0x00000004
#define MOUNTINFO_FLAG_IS_SAME_DEV 0x00000008
#define MOUNTINFO_FLAG_NO_STAT 0x00000010
#define MOUNTINFO_FLAG_NO_SIZE 0x00000020
#define MOUNTINFO_FLAG_READONLY 0x00000040

struct mountinfo {
    // mount ID: unique identifier of the mount (may be reused after umount(2)).
    int32_t id;
    // parent ID: ID of parent mount (or of self for the top of the mount tree).
    int32_t parent_id;

    uint32_t major;  // major: major device number of device.
    uint32_t minor;  // minor: minor device number of device.

    // char *   persistent_id; // a calculated persistent id for the mount point
    // uint32_t persistent_id_hash;

    // root: root of the mount within the filesystem.
    char *   root;
    uint32_t root_hash;

    // mount point: mount point relative to the process's root.
    char *   mount_point;
    uint32_t mount_point_hash;

    // mount options: per-mount options.
    char *  mount_options;
    int32_t optional_fields_count;

    // filesystem type: name of filesystem in the form "type[.subtype]".
    char *   filesystem;
    uint32_t filesystem_hash;

    // mount source: filesystem-specific information or "none".
    char *   mount_source;
    uint32_t mount_source_hash;

    uint32_t mount_source_name_hash;
    char *   mount_source_name;

    // super options: per-superblock options.
    char *super_options;

    uint32_t          flags;
    dev_t             st_dev;  // id of device as given by stat()
    struct mountinfo *next;
};

extern struct mountinfo *mountinfo_read(int do_statvfs);

extern void mountinfo_free_all(struct mountinfo *mi);