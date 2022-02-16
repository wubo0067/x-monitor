/*
 * @Author: CALM.WU
 * @Date: 2021-12-20 16:57:41
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2021-12-23 11:30:44
 */

#include "mountinfo.h"

#include "common.h"
#include "compiler.h"
#include "log.h"
#include "procfile.h"
#include "strings.h"

static const char *__mountinfo_file = "/proc/self/mountinfo";

// 区分是不是网络文件系统
#ifndef ME_REMOTE
/* A file system is "remote" if its Fs_name contains a ':'
   or if (it is of type (smbfs or cifs) and its Fs_name starts with '//')
   or Fs_name is equal to "-hosts" (used by autofs to mount remote fs).  */
#define ME_REMOTE(Fs_name, Fs_type)                                          \
    (strchr(Fs_name, ':') != NULL                                            \
     || ((Fs_name)[0] == '/' && (Fs_name)[1] == '/'                          \
         && (strcmp(Fs_type, "smbfs") == 0 || strcmp(Fs_type, "cifs") == 0)) \
     || (strcmp("-hosts", Fs_name) == 0))
#endif

#define ME_DUMMY_0(Fs_name, Fs_type)                                      \
    (strcmp(Fs_type, "autofs") == 0 || strcmp(Fs_type, "proc") == 0       \
     || strcmp(Fs_type, "subfs") == 0 /* for Linux 2.6/3.x */             \
     || strcmp(Fs_type, "debugfs") == 0 || strcmp(Fs_type, "devpts") == 0 \
     || strcmp(Fs_type, "fusectl") == 0 || strcmp(Fs_type, "mqueue") == 0 \
     || strcmp(Fs_type, "rpc_pipefs") == 0                                \
     || strcmp(Fs_type, "sysfs") == 0  /* FreeBSD, Linux 2.4 */           \
     || strcmp(Fs_type, "devfs") == 0  /* for NetBSD 3.0 */               \
     || strcmp(Fs_type, "kernfs") == 0 /* for Irix 6.5 */                 \
     || strcmp(Fs_type, "ignore") == 0)

/* Historically, we have marked as "dummy" any file system of type "none",
   but now that programs like du need to know about bind-mounted directories,
   we grant an exception to any with "bind" in its list of mount options.
   I.e., those are *not* dummy entries.  */
#define ME_DUMMY(Fs_name, Fs_type) (ME_DUMMY_0(Fs_name, Fs_type) || strcmp(Fs_type, "none") == 0)

// 判断文件系统是否只读
static int32_t is_readonly(const char *mount_options) {
    if (!mount_options) {
        return 0;
    }

    size_t len = strlen(mount_options);
    if (len < 2)
        return 0;
    if (len == 2) {
        if (!strcmp(mount_options, "ro"))
            return 1;
        return 0;
    }
    if (!strncmp(mount_options, "ro,", 3))
        return 1;
    if (!strncmp(&mount_options[len - 3], ",ro", 3))
        return 1;
    if (strstr(mount_options, ",ro,"))
        return 1;
    return 0;
}

static char *strdup_decoding_octal(const char *string) {
    char *buffer = strdup(string);

    char *      d = buffer;
    const char *s = string;

    while (*s) {
        if (unlikely(*s == '\\')) {
            s++;
            if (likely(isdigit(*s) && isdigit(s[1]) && isdigit(s[2]))) {
                char c = *s++ - '0';
                c <<= 3;
                c |= *s++ - '0';
                c <<= 3;
                c |= *s++ - '0';
                *d++ = c;
            } else
                *d++ = '_';
        } else
            *d++ = *s++;
    }
    *d = '\0';

    return buffer;
}

// read the whole moutinfo into a link list
struct mountinfo *mountinfo_read(int do_statvfs) {
    struct proc_file *pf = procfile_open(__mountinfo_file, " \t", PROCFILE_FLAG_DEFAULT);
    if (unlikely(!pf)) {
        error("Cannot open %s", __mountinfo_file);
        return NULL;
    }

    pf = procfile_readall(pf);
    if (unlikely(!pf)) {
        return NULL;
    }

    struct mountinfo *root = NULL, *last = NULL, *mi = NULL;
    uint32_t          l;
    uint32_t          lines = procfile_lines(pf);

    for (l = 0; l < lines; l++) {
        if (unlikely(procfile_linewords(pf, l) < 5)) {
            continue;
        }

        // 每一行都是一个mountinfo结构，文件系统
        mi = (struct mountinfo *)calloc(1, sizeof(struct mountinfo));

        // word的序号从0开始
        uint32_t w = 0;
        // id
        mi->id = strtol(procfile_lineword(pf, l, w), NULL, 10);
        w++;
        // parent id
        mi->parent_id = strtol(procfile_lineword(pf, l, w), NULL, 10);
        w++;

        // major
        char *major = procfile_lineword(pf, l, w);
        w++;
        char *minor;
        for (minor = major; *minor && *minor != ':'; minor++)
            ;

        if (unlikely(!*minor)) {
            error("Cannot parse major:minor on '%s' at line %u of '%s'", major, l + 1,
                  __mountinfo_file);
            free(mi);
            continue;
        }

        *minor = '\0';
        minor++;

        mi->flags = 0;
        mi->major = str2uint32_t(major);
        mi->minor = str2uint32_t(minor);

        mi->root = strdup(procfile_lineword(pf, l, w));
        w++;
        mi->root_hash = bkrd_hash(mi->root, strlen(mi->root));

        mi->mount_point = strdup(procfile_lineword(pf, l, w));
        w++;
        mi->mount_point_hash = bkrd_hash(mi->mount_point, strlen(mi->mount_point));

        // rw, nosuid...
        mi->mount_options = strdup(procfile_lineword(pf, l, w));
        w++;

        if (unlikely(is_readonly(mi->mount_options))) {
            mi->flags |= MOUNTINFO_FLAG_READONLY;
        }

        mi->optional_fields_count = 0;

        // example: shared:6
        char *s = procfile_lineword(pf, l, w);
        while (*s && *s != '-') {
            w++;
            s = procfile_lineword(pf, l, w);
            mi->optional_fields_count++;
        }

        if (likely(*s == '-')) {
            // 文件系统类型
            w++;

            mi->filesystem = strdup(procfile_lineword(pf, l, w));
            w++;
            mi->filesystem_hash = bkrd_hash(mi->filesystem, strlen(mi->filesystem));
            // 一个具体的device /dev/sda1
            mi->mount_source = strdup_decoding_octal(procfile_lineword(pf, l, w));
            w++;
            mi->mount_source_hash = bkrd_hash(mi->mount_source, strlen(mi->mount_source));

            mi->mount_source_name = strdup(basename(mi->mount_source));
            mi->mount_source_name_hash =
                bkrd_hash(mi->mount_source_name, strlen(mi->mount_source_name));

            mi->super_options = strdup(procfile_lineword(pf, l, w));
            w++;

            if (unlikely(is_readonly(mi->super_options))) {
                mi->flags |= MOUNTINFO_FLAG_READONLY;
            }

            if (unlikely(ME_DUMMY(mi->mount_source, mi->filesystem))) {
                mi->flags |= MOUNTINFO_FLAG_IS_DUMMY;
            }

            if (unlikely(ME_REMOTE(mi->mount_source, mi->filesystem))) {
                mi->flags |= MOUNTINFO_FLAG_IS_REMOTE;
            }

            if (do_statvfs) {
                // mark as BIND the duplicates (i.e. same filesystem + same source)
            } else {
                mi->st_dev = 0;
            }
        }

        //
        if (do_statvfs && !(mi->flags & MOUNTINFO_FLAG_IS_DUMMY)) {
            // check if it has size
        }

        // 插入链表
        if (unlikely(!root)) {
            root = mi;
        } else {
            last->next = mi;
        }

        last     = mi;
        mi->next = NULL;

        // debug("MOUNTINFO: %d %d %u:%u root '%s', mount point '%s', mount options '%s', "
        //       "filesystem '%s', mount source '%s', super options '%s'%s%s%s%s%s%s\n",
        //       mi->id, mi->parent_id, mi->major, mi->minor, mi->root,  // mi->persistent_id,
        //       (mi->mount_point) ? mi->mount_point : "",
        //       (mi->mount_options) ? mi->mount_options : "", (mi->filesystem) ? mi->filesystem :
        //       "", (mi->mount_source) ? mi->mount_source : "", (mi->super_options) ?
        //       mi->super_options : "", (mi->flags & MOUNTINFO_FLAG_IS_DUMMY) ? " DUMMY" : "",
        //       (mi->flags & MOUNTINFO_FLAG_IS_BIND) ? " BIND" : "",
        //       (mi->flags & MOUNTINFO_FLAG_IS_REMOTE) ? " REMOTE" : "",
        //       (mi->flags & MOUNTINFO_FLAG_NO_STAT) ? " NOSTAT" : "",
        //       (mi->flags & MOUNTINFO_FLAG_NO_SIZE) ? " NOSIZE" : "",
        //       (mi->flags & MOUNTINFO_FLAG_IS_SAME_DEV) ? " SAMEDEV" : "");
    }

    procfile_close(pf);
    pf = NULL;
    return root;
}

static void mountinfo_free(struct mountinfo *mi) {
    free(mi->root);
    free(mi->mount_point);
    free(mi->mount_options);
    // free(mi->persistent_id);

    free(mi->filesystem);
    free(mi->mount_source);
    free(mi->mount_source_name);
    free(mi->super_options);
    free(mi);
}

void mountinfo_free_all(struct mountinfo *mi) {
    while (mi) {
        struct mountinfo *next = mi->next;
        mountinfo_free(mi);
        mi = next;
    }
}