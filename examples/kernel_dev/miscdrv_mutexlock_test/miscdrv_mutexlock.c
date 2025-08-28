/*
 * @Author: CALM.WU
 * @Date: 2025-08-28 14:44:04
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2025-08-28 14:50:15
 */
#define pr_fmt(fmt) "%s:%s(): " fmt, KBUILD_MODNAME, __func__

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/uaccess.h>

#include "../kutils/misc.h"
#include "../kutils/utils.h"

#define MODULE_TAG "Module:[cw_miscdrv_mutexlock]"

struct cw_miscdrv_ctx {
    struct device *dev;
    int32_t tx, rx, err, word;
    uint32_t config1, config2;
    uint64_t config3;

#define MAXBYTES 128
    char oursecret[MAXBYTES];
    struct mutex lock;
};

static struct cw_miscdrv_ctx *__cw_miscdrv_ctx;
static int32_t __ga, __gb = 1;
DEFINE_MUTEX(__g_mutex);

static int32_t __cw_miscdrv_mutexlock_open(struct inode *inode,
                                           struct file *flip)
{
    struct device *dev = __cw_miscdrv_ctx ? __cw_miscdrv_ctx->dev : NULL;
    int ga_copy, gb_copy;

    // 参数检查
    if (unlikely(!inode || !flip)) {
        pr_err(MODULE_TAG "invalid parameters\n");
        return -EINVAL;
    }

    // 上下文检查
    if (unlikely(!__cw_miscdrv_ctx || !dev)) {
        pr_err(MODULE_TAG "driver context not initialized\n");
        return -ENODEV;
    }

    PRINT_CTX();

    // 在临界区内完成所有对共享变量的访问
    mutex_lock(&__g_mutex);
    __ga++;
    __gb--;
    ga_copy = __ga;
    gb_copy = __gb;
    mutex_unlock(&__g_mutex);

    dev_info(dev,
             MODULE_TAG " filename: \"%s\"\n"
                        " wrt open file: f_flags = 0x%x\n"
                        " ga = %d, gb = %d\n",
             flip->f_path.dentry->d_name.name, flip->f_flags, ga_copy, gb_copy);

    return 0;
}

static ssize_t __cw_miscdrv_mutexlock_read(struct file *filp, char __user *ubuf,
                                           size_t count, loff_t *f_pos)
{
    ssize_t ret = count;
    struct device *dev = __cw_miscdrv_ctx->dev;
    size_t secret_len;

    PRINT_CTX();
    dev_info(dev, MODULE_TAG " %s want to read %zu bytes\n", current->comm,
             count);

    ret = -EINVAL;
    if (count < MAXBYTES) {
        // 读取的缓冲区小于加密缓冲区，返回错误
        dev_warn(dev,
                 MODULE_TAG
                 " request %zu bytes is < required size %d, aborting read\n",
                 count, MAXBYTES);
        goto out;
    }

    mutex_lock(&__cw_miscdrv_ctx->lock);
    secret_len = strnlen(__cw_miscdrv_ctx->oursecret, MAXBYTES);

    // 如果 secret_len 小于等于 0，表明加密缓冲区无效，无法读取秘钥
    if (secret_len <= 0) {
        dev_warn(dev,
                 MODULE_TAG " the secret isn't available, aborting read\n");
        ret = -EFAULT;
        goto out_unlock;
    }

    ret = -EFAULT;
    if (copy_to_user(ubuf, __cw_miscdrv_ctx->oursecret, secret_len)) {
        dev_warn(dev, MODULE_TAG " copy_to_user() failed\n");
        goto out_unlock;
    }

    __cw_miscdrv_ctx->tx += secret_len;
    ret = secret_len;
    dev_info(dev,
             MODULE_TAG " %zd bytes read, returning... (stats: tx=%d, rx=%d)\n",
             secret_len, __cw_miscdrv_ctx->tx, __cw_miscdrv_ctx->rx);

out_unlock:
    mutex_unlock(&__cw_miscdrv_ctx->lock);
out:
    return ret;
}

static const struct file_operations __cw_miscdrv_mutexlock_fops = {
    .owner = THIS_MODULE,
    .open = __cw_miscdrv_mutexlock_open,
    .read = __cw_miscdrv_mutexlock_read,
};

static struct miscdevice __cw_miscdrv_mutexlock_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "cw_miscdrv_mutexlock",
    .fops = &__cw_miscdrv_mutexlock_fops,
    .mode = 0666,
};

static int32_t __init __cw_miscdrv_mutexlock_init(void)
{
    int32_t ret;

    ret = misc_register(&__cw_miscdrv_mutexlock_dev);
    if (ret) {
        pr_err(MODULE_TAG "misc_register() failed, err: %d\n", ret);
        return ret;
    }
    pr_info(MODULE_TAG "misc_register() OK, minor: %d, dev node is /dev/%s\n",
            __cw_miscdrv_mutexlock_dev.minor, __cw_miscdrv_mutexlock_dev.name);

    __cw_miscdrv_ctx = devm_kzalloc(__cw_miscdrv_mutexlock_dev.this_device,
                                    sizeof(struct cw_miscdrv_ctx), GFP_KERNEL);
    if (unlikely(!__cw_miscdrv_ctx)) {
        pr_err(MODULE_TAG "devm_kzalloc() for misc_drv_ctx failed\n");
        misc_deregister(&__cw_miscdrv_mutexlock_dev);
        return -ENOMEM;
    }

    mutex_init(&__cw_miscdrv_ctx->lock);

    __cw_miscdrv_ctx->dev = __cw_miscdrv_mutexlock_dev.this_device;
    strscpy(__cw_miscdrv_ctx->oursecret, "default_secret", MAXBYTES);

    dev_dbg(__cw_miscdrv_ctx->dev, MODULE_TAG " init done\n");
    return 0;
}

static void __exit __cw_miscdrv_mutexlock_exit(void)
{
    misc_deregister(&__cw_miscdrv_mutexlock_dev);
    mutex_destroy(&__cw_miscdrv_ctx->lock);
    pr_info(MODULE_TAG " exit done\n");
}

module_init(__cw_miscdrv_mutexlock_init);
module_exit(__cw_miscdrv_mutexlock_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION(
        "cw_miscdrv_mutexlock - A test module for miscdrv mutex lock");
MODULE_VERSION("0.0.1");
