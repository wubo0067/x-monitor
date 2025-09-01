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
    size_t secret_len, read_len;

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
    // 得到字符串长度
    secret_len = strnlen(__cw_miscdrv_ctx->oursecret, MAXBYTES);

    // 如果 secret_len 小于等于 0，表明加密缓冲区无效，无法读取秘钥
    if (secret_len <= 0) {
        dev_warn(dev,
                 MODULE_TAG " the secret isn't available, aborting read\n");
        ret = -EFAULT;
        goto out_unlock;
    }

    // 检查是否已经读取到文件末尾
    if (*f_pos >= (loff_t)secret_len) {
        dev_info(dev, MODULE_TAG " EOF: f_pos=%lld >= secret_len=%zu\n", *f_pos,
                 secret_len);
        ret = 0; // 返回 EOF
        goto out_unlock;
    }

    // 计算实际可读取的字节数
    read_len = min(count, (size_t)(secret_len - (size_t)*f_pos));

    ret = -EFAULT;
    if (copy_to_user(ubuf, __cw_miscdrv_ctx->oursecret + *f_pos, read_len)) {
        dev_warn(dev, MODULE_TAG " copy_to_user() failed\n");
        goto out_unlock;
    }

    __cw_miscdrv_ctx->tx += read_len;
    // 修改文件偏移，如果不修改，那么 cat 还是会认为有数据可读，导致无限循环了。
    *f_pos += read_len;
    ret = read_len;
    dev_info(dev,
             MODULE_TAG " %zd bytes read, returning... (stats: tx=%d, rx=%d)\n",
             read_len, __cw_miscdrv_ctx->tx, __cw_miscdrv_ctx->rx);

out_unlock:
    mutex_unlock(&__cw_miscdrv_ctx->lock);
out:
    return ret;
}

static ssize_t __cw_miscdrv_mutexlock_write(struct file *filp,
                                            const char __user *ubuf,
                                            size_t count, loff_t *f_pos)
{
    ssize_t ret;
    struct device *dev = __cw_miscdrv_ctx->dev;
    size_t copy_len;

    // 参数验证
    if (unlikely(!filp || !ubuf)) {
        pr_err(MODULE_TAG "invalid parameters\n");
        return -EINVAL;
    }

    if (unlikely(count == 0)) {
        pr_warn(MODULE_TAG "zero-length write request\n");
        return 0;
    }

    PRINT_CTX();
    dev_info(dev, MODULE_TAG " %s want to write %zu bytes\n", current->comm,
             count);

    mutex_lock(&__cw_miscdrv_ctx->lock);

    // 覆盖写，始终从头写入
    copy_len = min(count, (size_t)(MAXBYTES - 1));

    // 清空目标缓冲区中将要写入的部分
    memset(__cw_miscdrv_ctx->oursecret + *f_pos, 0, copy_len);

    // 直接从用户空间复制到 oursecret 缓冲区
    if (copy_from_user(__cw_miscdrv_ctx->oursecret, ubuf, copy_len)) {
        dev_err(dev, MODULE_TAG " copy_from_user() failed\n");
        ret = -EFAULT;
        goto out_unlock;
    }

    // 确保字符串以\0结尾
    __cw_miscdrv_ctx->oursecret[copy_len] = '\0';

    // 更新统计
    __cw_miscdrv_ctx->rx += copy_len;
    ret = copy_len;

    dev_info(dev,
             MODULE_TAG
             " %zd bytes written, returning... (stats: tx=%d, rx=%d)\n",
             copy_len, __cw_miscdrv_ctx->tx, __cw_miscdrv_ctx->rx);

    // 覆盖写，重置文件偏移
    if (f_pos)
        *f_pos = 0;

out_unlock:
    mutex_unlock(&__cw_miscdrv_ctx->lock);
    return ret;
}

// ... existing code ...
static int32_t __cw_miscdrv_mutexlock_release(struct inode *inode,
                                              struct file *filp)
{
    struct device *dev = __cw_miscdrv_ctx->dev;
    int local_ga, local_gb;
    const char *filename;

    if (!inode || !filp) {
        return -EINVAL;
    }

    PRINT_CTX();

    /* 获取文件名，不需要在临界区内完成 */
    filename = filp->f_path.dentry->d_name.name;

    mutex_lock(&__cw_miscdrv_ctx->lock);
    __ga--;
    __gb++;
    local_ga = __ga;
    local_gb = __gb;
    mutex_unlock(&__cw_miscdrv_ctx->lock);

    dev_info(dev, MODULE_TAG " %s close filename: \"%s\"\n ga=%d, gb=%d\n",
             current->comm, filename, local_ga, local_gb);

    return 0;
}
// ... existing code ...

static const struct file_operations __cw_miscdrv_mutexlock_fops = {
    .owner = THIS_MODULE,
    .open = __cw_miscdrv_mutexlock_open,
    .read = __cw_miscdrv_mutexlock_read,
    .write = __cw_miscdrv_mutexlock_write,
    .release = __cw_miscdrv_mutexlock_release,
    .llseek = no_llseek,
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
