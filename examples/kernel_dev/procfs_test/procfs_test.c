/*
 * @Author: CALM.WU
 * @Date: 2024-04-10 14:34:01
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-08 14:37:12
 */
/*
 * /proc
 *  ...
 *  |---cw_procfs_test           <-- our proc directory
 *      |---dbg_level
 *      |---show_pgoff
 *      |---show_drvctx
 *      |---config
 */

#define pr_fmt(fmt) "%s:%s():%d: " fmt, KBUILD_MODNAME, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/uaccess.h> // for copy_to/from_user()
#include <linux/slab.h>    // for kmalloc()
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/random.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

// 如果编译没有代入版本信息
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#ifndef LINUX_VERSION_CODE
#error "LINUX_VERSION_CODE is undefined"
#else
// 打印 LINUX_VERSION_CODE 宏的值
#pragma message("LINUX_VERSION_CODE: " __stringify(LINUX_VERSION_CODE))
#endif

#ifndef KERNEL_VERSION
#error "KERNEL_VERSION is undefined"
#endif

#define PROCFS_TEST_DBG_LEVEL "dbg_level"
#define PROCFS_TEST_DBG_LEVEL_PERMS 0644
#define PROCFS_TEST_SHOW_PGOFF "show_pgoff"
#define PROCFS_TEST_SHOW_PGOFF_PERMS 0444
#define PROCFS_TEST_SHOW_DRVCTX "show_drvctx"
#define PROCFS_TEST_SHOW_DRVCTX_PERMS 0440

#define PROC_TEST_DIRNAME "cw_procfs_test"
#define KEY_SIZE 128

#define DEBUG_LEVEL_MIN 0
#define DEBUG_LEVEL_MAX 2
#define DEBUG_LEVEL_DEFAULT DEBUG_LEVEL_MIN

static DEFINE_MUTEX(__mutex);

static struct proc_dir_entry *__cw_procfs_dir = NULL;

struct cw_drv_ctx {
    int32_t tx, rx, word, power;
    int32_t debug_level;
    char key[KEY_SIZE];
};
static struct cw_drv_ctx *__cw_drv_ctx;

/***************** sysfs /sys/kernel/cw_test *****************/
#define SYSFS_TEST_DIRNAME "cw_test"
static int32_t cw_num = 0;
static ssize_t __cw_sysfs_num_show(struct kobject *kobj,
                                   struct kobj_attribute *attr, char *buf)
{
    pr_info("/sys/kernel/cw_test/cw_num read\n");
    return sprintf(buf, "%d\n", cw_num);
}

static ssize_t __cw_sysfs_num_store(struct kobject *kobj,
                                    struct kobj_attribute *attr,
                                    const char *buf, size_t count)
{
    pr_info("/sys/kernel/cw_test/cw_num write\n");
    sscanf(buf, "%du", &cw_num);
    return count;
}

static struct kobj_attribute __cw_num_attr =
        __ATTR(cw_num, 0660, __cw_sysfs_num_show, __cw_sysfs_num_store);

static struct kobject *__cw_test_kobj = NULL;

//--------------------------------------------------------
static struct cw_drv_ctx *__alloc_cw_drv_ctx(void)
{
    struct cw_drv_ctx *dc = NULL;

    dc = kzalloc(sizeof(struct cw_drv_ctx), GFP_KERNEL);
    if (unlikely(!dc)) {
        pr_err("kzalloc struct cw_drv_ctx failed.\n");
        // 将错误吗封装成一个特殊的指针值，获取这个指针后，使用 IS_ERR
        // 来判断是不是一个错误指针，用 PTR_ERR 来获得错误码 #define
        // IS_ERR_VALUE(x) unlikely((x) >= (unsigned long)-MAX_ERRNO),
        // 实际上内核地址是不可能大于 (unsigned long)-MAX_ERRNO 的
        return ERR_PTR(-ENOMEM);
    }

    dc->debug_level = DEBUG_LEVEL_DEFAULT;
    // 随机生成一个 128 字节的 key
    get_random_bytes(dc->key, KEY_SIZE);

    return dc;
}

//--------------------------------------------------------
// for /proc/cw_procfs_test/show_drvctx
static int32_t __cw_show_drvctx(struct seq_file *seq, void *data)
{
    if (mutex_lock_interruptible(&__mutex))
        return -ERESTARTSYS;

    seq_printf(seq, "tx:%d, rx:%d, word:%d, power:%d, dbg_level:%d, key:‘%s’\n",
               __cw_drv_ctx->tx, __cw_drv_ctx->rx, __cw_drv_ctx->word,
               __cw_drv_ctx->power, __cw_drv_ctx->debug_level,
               __cw_drv_ctx->key);
    __cw_drv_ctx->rx++;
    mutex_unlock(&__mutex);
    return 0;
}

static int32_t __cw_open_drvctx(struct inode *in, struct file *f)
{
    return single_open(f, __cw_show_drvctx, NULL);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
static struct file_operations __cw_fops_show_drvctx = {
    .owner = THIS_MODULE,
    .open = __cw_open_drvctx,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static struct proc_fops __cw_fops_show_drvctx = {
    .proc_open = __cw_open_drvctx,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

//--------------------------------------------------------
// for /proc/cw_procfs_test/dbg_level

static ssize_t __cw_write_dbg_level(struct file *f, const char __user *ubuf,
                                    size_t count, loff_t *off)
{
    int32_t ret = count;
    char buf[12];

    // 可以被信号中断，例如 CTRL+C，在用户交互的处理中，最好使用可中断的 mutex
    if (mutex_lock_interruptible(&__mutex))
        // 被信号中断，返回 -ERESTARTSYS，调用方应该重新调用
        return -ERESTARTSYS;

    if (count == 0 || count > 12) {
        pr_err("Invalid key size\n");
        ret = -EINVAL;
        goto out;
    }

    // 从用户空间拷贝数据到内核空间
    if (copy_from_user(buf, ubuf, count)) {
        pr_err("copy_from_user failed\n");
        ret = -EFAULT;
        goto out;
    }
    buf[count - 1] = '\0';
    pr_info("user write buf = %s\n", buf);
    ret = kstrtoint(buf, 10, &__cw_drv_ctx->debug_level);
    if (ret) {
        pr_err("kstrtoint failed\n");
        goto out;
    }

    if (__cw_drv_ctx->debug_level < DEBUG_LEVEL_MIN ||
        __cw_drv_ctx->debug_level > DEBUG_LEVEL_MAX) {
        pr_err("trying to set invalid value for debug_level\n"
               " [allowed range: %d-%d]\n",
               DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX);
        ret = -EINVAL;
        goto out;
    }

    __cw_drv_ctx->tx++;
    ret = count;
out:
    mutex_unlock(&__mutex);
    return ret;
}

static int32_t __cw_show_dbg_level(struct seq_file *seq, void *data)
{
    // 可以被信号中断，例如 CTRL+C，在用户交互的处理中，最好使用可中断的 mutex
    if (mutex_lock_interruptible(&__mutex))
        // 被信号中断，返回 -ERESTARTSYS，调用方应该重新调用
        return -ERESTARTSYS;
    seq_printf(seq, "debug_level:%d\n", __cw_drv_ctx->debug_level);
    __cw_drv_ctx->rx++;
    mutex_unlock(&__mutex);
    return 0;
}

static int32_t __cw_open_dbg_level(struct inode *in, struct file *f)
{
    return single_open(f, __cw_show_dbg_level, NULL);
}

// proc file operation 在内核 5.6.0 以上改为 struct proc_ops
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
static struct file_operations __cw_fops_dbg_level = {
    .owner = THIS_MODULE,
    .open = __cw_open_dbg_level,
    .read = seq_read,
    .write = __cw_write_dbg_level,
    .llseek = seq_lseek,
    .release = single_release,
};
#else
static struct proc_fops __cw_fops_dbg_level = {
    .proc_open = __cw_open_dbg_level,
    .proc_read = seq_read,
    .proc_write = __cw_write_dbg_level,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#endif

//--------------------------------------------------------
// for /proc/cw_procfs_test/show_pgoff
static int32_t __cw_show_pgoff(struct seq_file *seq, void *data)
{
    seq_printf(seq, "%s: PAGE_OFFSET:0x%px, PAGE_SIZE:%lu\n", PROC_TEST_DIRNAME,
               (void *)PAGE_OFFSET, PAGE_SIZE);
    __cw_drv_ctx->rx++;
    return 0;
}

//--------------------------------------------------------
// 模块初始化，注销函数
static int32_t __init cw_procfs_test_init(void)
{
    int32_t ret = 0;

    // 判读是否支持 procfs
    if (unlikely(!IS_ENABLED(CONFIG_PROC_FS))) {
        pr_warn("procfs unsupported! Aborting ...\n");
        return -EINVAL;
    }

    __cw_drv_ctx = __alloc_cw_drv_ctx();
    if (IS_ERR(__cw_drv_ctx)) {
        pr_err("device context alloc failed.\n");
        ret = PTR_ERR(__cw_drv_ctx);
        return ret;
    }

    // 创建/proc 下的 parent dir
    __cw_procfs_dir = proc_mkdir(PROC_TEST_DIRNAME, NULL);
    if (unlikely(!__cw_procfs_dir)) {
        pr_err("[module]:procfs_test mkdir '/proc/%s' failed.\n",
               PROC_TEST_DIRNAME);
        // 设置返回码
        ret = -ENOMEM;
        goto init_fail_1;
    }
    pr_info("[module]:procfs_test Successfully created the procfs dir: "
            "'/proc/%s'.\n",
            PROC_TEST_DIRNAME);

    // 创建 dbg_level entry
    if (!proc_create(PROCFS_TEST_DBG_LEVEL, PROCFS_TEST_DBG_LEVEL_PERMS,
                     __cw_procfs_dir, &__cw_fops_dbg_level)) {
        pr_err("[module]:procfs_test failed to create /proc/%s/%s\n",
               PROC_TEST_DIRNAME, PROCFS_TEST_DBG_LEVEL);
        ret = -ENOMEM;
        goto init_fail_2;
    }
    pr_info("[module]:procfs_test Successfully created the '/proc/%s/%s'.\n",
            PROC_TEST_DIRNAME, PROCFS_TEST_DBG_LEVEL);

    // 创建 show_pgoff
    if (!proc_create_single_data(PROCFS_TEST_SHOW_PGOFF,
                                 PROCFS_TEST_SHOW_PGOFF_PERMS, __cw_procfs_dir,
                                 __cw_show_pgoff, 0)) {
        pr_err("[module]:procfs_test failed to create /proc/%s/%s\n",
               PROC_TEST_DIRNAME, PROCFS_TEST_SHOW_PGOFF);
        ret = -ENOMEM;
        goto init_fail_2;
    }
    pr_info("[module]:procfs_test Successfully created the '/proc/%s/%s'.\n",
            PROC_TEST_DIRNAME, PROCFS_TEST_SHOW_PGOFF);

    // 创建 show_drvctx
    if (!proc_create(PROCFS_TEST_SHOW_DRVCTX, PROCFS_TEST_SHOW_DRVCTX_PERMS,
                     __cw_procfs_dir, &__cw_fops_show_drvctx)) {
        pr_err("[module]:procfs_test failed to create /proc/%s/%s.\n",
               PROC_TEST_DIRNAME, PROCFS_TEST_SHOW_DRVCTX);
        ret = -ENOMEM;
        goto init_fail_2;
    }
    pr_info("[module]:procfs_test Successfully created the '/proc/%s/%s'.\n",
            PROC_TEST_DIRNAME, PROCFS_TEST_SHOW_DRVCTX);

    // create a directory in /sys/kernel
    __cw_test_kobj = kobject_create_and_add(SYSFS_TEST_DIRNAME, kernel_kobj);
    if (!__cw_test_kobj) {
        pr_err("[module] procfs_test: Failed to create kernel kobj\n");
        ret = -ENOMEM;
        goto init_fail_3;
    }
    pr_info("[module] procfs_test: Successfully created the /sys/kernel/%s.\n",
            SYSFS_TEST_DIRNAME);
    // create a file in /sys/kernel/cw_test
    ret = sysfs_create_file(__cw_test_kobj, &__cw_num_attr.attr);
    if (ret) {
        pr_err("[module] procfs_test: Failed to create /sys/kernel/%s/cw_num\n",
               SYSFS_TEST_DIRNAME);
        goto init_fail_3;
    }
    pr_info("[module] procfs_test: Successfully created the "
            "/sys/kernel/%s/cw_num.\n",
            SYSFS_TEST_DIRNAME);

    pr_info("[module] procfs_test init ok!!!\n");
    return 0;

init_fail_3:
    kobject_put(__cw_test_kobj);
    sysfs_remove_file(kernel_kobj, &__cw_num_attr.attr);

init_fail_2:
    // 删除/proc/cw_procfs_test 目录
    remove_proc_subtree(PROC_TEST_DIRNAME, NULL);
init_fail_1:
    kzfree(__cw_drv_ctx);

    return ret;
}

static void __exit cw_procfs_test_exit(void)
{
    kobject_put(__cw_test_kobj);
    sysfs_remove_file(kernel_kobj, &__cw_num_attr.attr);
    // 上次/proc/cw_procfs_test 目录树
    remove_proc_subtree(PROC_TEST_DIRNAME, NULL);
    if (!IS_ERR(__cw_drv_ctx))
        kzfree(__cw_drv_ctx);
    pr_info("[module] procfs_test exit\n");
}

module_init(cw_procfs_test_init);
module_exit(cw_procfs_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("ProcFS Example");
MODULE_VERSION("0.1");