/*
 * @Author: calmwu
 * @Date: 2024-06-15 11:23:28
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-08 14:14:56
 */

#define pr_fmt(fmt) "%s:%s():%d: " fmt, KBUILD_MODNAME, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>
#include <linux/semaphore.h>
#include <linux/major.h>

// 如果编译没有代入版本信息
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#include <kutils.h>
#include <misc.h>
#include "dev_ioctl_cmd.h"

#define MODULE_TAG "Module:[cw_ioctl_test]"
#define CW_IOCTL_DEV_MAJOR 0
#define CW_IOCTL_NR_DEVS 1

#define CW_DEV_NAME "cw_ioctl_dev"
#define CW_DEV_NAME_FMT "cw_ioctl_dev%d"

struct cw_ioctl_dev {
    char *buf;
    size_t len;
    int32_t debug_level;
    struct semaphore sem;
};

static struct cw_ioctl_dev *__cw_ioctl_devs = NULL;

// 定义模块参数和变量
// /sys/module/cw_dev_ioctl_test
static int32_t __cw_ioctl_dev_major = CW_IOCTL_DEV_MAJOR;
module_param(__cw_ioctl_dev_major, int, S_IRUGO);

static int32_t __cw_ioctl_dev_minor = 0;
module_param(__cw_ioctl_dev_minor, int, S_IRUGO);

static int32_t __cw_ioctl_nr_devs = CW_IOCTL_NR_DEVS;
module_param(__cw_ioctl_nr_devs, int, S_IRUGO);

// 设备创建上下文
static struct cw_cdev_crt_ctx __dev_crt_ctx;

// 设备文件操作
static int32_t __ioctl_dev_open(struct inode *inode, struct file *filp);
static int32_t __ioctl_dev_release(struct inode *inode, struct file *filp);
static ssize_t __ioctl_dev_read(struct file *filp, char __user *buf,
                                size_t count, loff_t *f_pos);
static ssize_t __ioctl_dev_write(struct file *filp, const char __user *buf,
                                 size_t count, loff_t *f_pos);
static long __ioctl_dev_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg);

static struct file_operations __ioctl_dev_fops = {
    .owner = THIS_MODULE,
    .open = __ioctl_dev_open,
    .release = __ioctl_dev_release,
    .read = __ioctl_dev_read,
    .write = __ioctl_dev_write,
    .unlocked_ioctl = __ioctl_dev_ioctl,
};

static ssize_t cw_thrdlist_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    ssize_t read_bytes = 0;
    read_bytes = show_thrdlist(buf, (ssize_t)PAGE_SIZE);
    pr_info(MODULE_TAG " show_thrdlist: read_bytes=%ld\n", read_bytes);
    return read_bytes;
}

// static ssize_t cw_ioctl_dev_debug_level_store(struct device *dev,
//                                               struct device_attribute *attr,
//                                               const char *buf, size_t count)
// {
//     ssize_t ret = (ssize_t)count;
//     return ret;
// }

// /sys/devices/virtual/cw_ioctl_dev/cw_ioctl_dev0/cw_thrdlist
static DEVICE_ATTR_RO(cw_thrdlist);

// dev_attr_show 函数会调用 tasklist_show 函数
// 返回写入 buf 的字节数，如果大于 PAGE_SIZE 会报错
static ssize_t cw_tasklist_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    ssize_t read_bytes = 0;
    read_bytes = show_tasklist(buf, (ssize_t)PAGE_SIZE);
    pr_info(MODULE_TAG " show_tasklist: read_bytes=%ld\n", read_bytes);
    return read_bytes;
}

// /sys/devices/virtual/cw_ioctl_dev/cw_ioctl_dev0/cw_tasklist
static DEVICE_ATTR_RO(cw_tasklist);

static int32_t __ioctl_dev_open(struct inode *inode, struct file *filp)
{
    // 获得通用的设备结构指针
    struct cw_dev *dev = container_of(inode->i_cdev, struct cw_dev, cdev);
    // 将 cw_ioctl_dev 结构指针赋值给 file 的私有信息
    filp->private_data = dev;
    pr_info(MODULE_TAG " opened device[%d:%d]\n", MAJOR(dev->devt),
            MINOR(dev->devt));
    return 0;
}

static int32_t __ioctl_dev_release(struct inode *inode, struct file *filp)
{
    struct cw_dev *dev = filp->private_data;
    pr_info(MODULE_TAG " released device[%d:%d]\n", MAJOR(dev->devt),
            MINOR(dev->devt));
    return 0;
}

/*
f_pos: 读取操作完成后，驱动程序可以更新这个偏移量以反映新的读位置。
*/
static ssize_t __ioctl_dev_read(struct file *filp, char __user *buf,
                                size_t count, loff_t *f_pos)
{
    ssize_t ret = 0;
    struct cw_dev *dev = filp->private_data;
    struct cw_ioctl_dev *ci_dev = (struct cw_ioctl_dev *)dev->extern_data;

    // pr_info(MODULE_TAG " will read from device[%d:%d]\n", MAJOR(dev->devt),
    //         MINOR(dev->devt));

    if (down_interruptible(&ci_dev->sem)) {
        return -ERESTARTSYS;
    }

    // 判断偏移是否超过缓冲区大小
    // if (*f_pos >= ci_dev->len) {
    //     goto out;
    // }

    // // 判断从偏移开始读取的字节数是否超过缓冲区大小，计算可以读取的字节数
    // if (*f_pos + count > ci_dev->len) {
    //     count = ci_dev->len - *f_pos;
    // }

    // if (count > 0) {
    //     if (copy_to_user(buf, ci_dev->buf + *f_pos, count)) {
    //         ret = -EFAULT;
    //     } else {
    //         *f_pos += count; //更新偏移
    //         ret = count;
    //     }
    // }
    ret = simple_read_from_buffer(buf, count, f_pos, ci_dev->buf, ci_dev->len);
    if (ret >= 0) {
        pr_info(MODULE_TAG " read %ld bytes from device[%d:%d].\n", ret,
                MAJOR(dev->devt), MINOR(dev->devt));
    } else {
        pr_err(MODULE_TAG " read from device[%d:%d] failed. ret:%ld\n",
               MAJOR(dev->devt), MINOR(dev->devt), ret);
    }

    up(&ci_dev->sem);
    return ret;
}

/*
返回值：如果写入操作成功，返回值为实际写入的字节数，如果写入操作失败，返回值为负数。
    -EFAULT：表示无效的内存地址或无法访问的用户空间缓冲区。
    -ENOMEM：表示内存不足。
*/
static ssize_t __ioctl_dev_write(struct file *filp, const char __user *buf,
                                 size_t count, loff_t *f_pos)
{
    ssize_t ret = 0;
    struct cw_dev *dev = filp->private_data;
    struct cw_ioctl_dev *ci_dev = (struct cw_ioctl_dev *)dev->extern_data;

    if (count == 0) {
        pr_warn(MODULE_TAG " write nothing to device[%d:%d]\n",
                MAJOR(dev->devt), MINOR(dev->devt));
        return 0;
    }

    if (down_interruptible(&ci_dev->sem)) {
        pr_warn(MODULE_TAG " write interrupted\n");
        return -ERESTARTSYS;
    }

    // 释放 dev 的空间
    kfree(ci_dev->buf);
    ci_dev->buf = NULL;
    ci_dev->len = 0;

    // 重新分配
    ci_dev->buf = kzalloc(count, GFP_KERNEL);
    if (NULL == ci_dev->buf) {
        pr_err(MODULE_TAG " write failed, no memory\n");
        ret = -ENOMEM;
        goto out;
    }
    ret = simple_write_to_buffer(ci_dev->buf, count, f_pos, buf, count);
    if (ret < 0) {
        pr_err(MODULE_TAG
               " write to device[%d:%d] failed, copy_to_user failed\n",
               MAJOR(dev->devt), MINOR(dev->devt));
        goto copy_failed;
    }
    ci_dev->len = ret;
    print_hex_dump_bytes(MODULE_TAG "User buffer: ", DUMP_PREFIX_OFFSET,
                         ci_dev->buf, ci_dev->len);
    pr_info(MODULE_TAG " write %lu bytes to device[%d:%d]\n", ret,
            MAJOR(dev->devt), MINOR(dev->devt));
    goto out;

copy_failed:
    kfree(ci_dev->buf);
out:
    up(&ci_dev->sem);

    return ret;
}

static long __ioctl_dev_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    int32_t ret = 0, tmp;
    struct cw_dev *dev = filp->private_data;
    struct cw_ioctl_dev *ci_dev = (struct cw_ioctl_dev *)dev->extern_data;

    pr_info(MODULE_TAG " ioctl device[%d:%d], cmd=%d\n", MAJOR(dev->devt),
            MINOR(dev->devt), _IOC_NR(cmd));

    // 校验 cmd 类型
    if (unlikely(_IOC_TYPE(cmd) != CW_IOCTL_MAGIC)) {
        pr_err(MODULE_TAG " ioctl device[%d:%d], invalid cmd magic\n",
               MAJOR(dev->devt), MINOR(dev->devt));
        return -ENOTTY;
    }
    // 校验 cmd 序号
    if (_IOC_NR(cmd) > CW_IOCTL_MAX_NRS) {
        pr_err(MODULE_TAG " ioctl device[%d:%d], invalid cmd nr\n",
               MAJOR(dev->devt), MINOR(dev->devt));
        return -ENOTTY;
    }

    switch (cmd) {
    case CW_IOCTL_RESET:
        pr_info(MODULE_TAG " ioctl device[%d:%d], reset\n", MAJOR(dev->devt),
                MINOR(dev->devt));
        down(&ci_dev->sem);
        kfree(ci_dev->buf);
        ci_dev->len = 0;
        up(&ci_dev->sem);
        break;
    case CW_IOCTL_GET_LENS:
        pr_info(MODULE_TAG " ioctl device[%d:%d], get buf len\n",
                MAJOR(dev->devt), MINOR(dev->devt));
        down(&ci_dev->sem);
        ret = __put_user(ci_dev->len, (int32_t __user *)arg);
        up(&ci_dev->sem);
        break;
    case CW_IOCTL_SET_LENS:
        down(&ci_dev->sem);
        ret = __get_user(tmp, (int32_t __user *)arg);
        if (ci_dev->len > tmp) {
            ci_dev->len = tmp;
            pr_info(MODULE_TAG " ioctl device[%d:%d], set buf len:%ld\n",
                    MAJOR(dev->devt), MINOR(dev->devt), ci_dev->len);
        }
        up(&ci_dev->sem);
        break;
    default:
        break;
    }
    return ret;
}

static int32_t __init cw_ioctl_test_init(void)
{
    int32_t ret = 0;
    int32_t i = 0;

    __dev_crt_ctx.major = __cw_ioctl_dev_major;
    __dev_crt_ctx.base_minor = __cw_ioctl_dev_minor;
    __dev_crt_ctx.count = __cw_ioctl_nr_devs; // 设备数量
    __dev_crt_ctx.name = CW_DEV_NAME;
    __dev_crt_ctx.fops = &__ioctl_dev_fops;
    __dev_crt_ctx.name_fmt = CW_DEV_NAME_FMT;
    __dev_crt_ctx.dev_priv_data = NULL;

    ret = module_create_cdevs(&__dev_crt_ctx);
    if (unlikely(0 != ret)) {
        pr_err(MODULE_TAG " module_create_cdevs failed\n");
        return ret;
    }

    __cw_ioctl_devs =
            (struct cw_ioctl_dev *)kmalloc_array(__cw_ioctl_nr_devs,
                                                 sizeof(struct cw_ioctl_dev),
                                                 GFP_KERNEL | __GFP_ZERO);
    if (!__cw_ioctl_devs) {
        pr_err(MODULE_TAG " kmalloc_array cw_ioctl_devs failed.\n");
        ret = -ENOMEM;
        goto out1;
    }

    for (i = 0; i < __cw_ioctl_nr_devs; i++) {
        // __dev_crt_ctx.devs[i].extern_data = kasprintf(
        //         GFP_KERNEL, "~~this cw_ioctl_dev_%d extern data~~", i);
        // 初始化信号量
        sema_init(&__cw_ioctl_devs[i].sem, 1);
        __dev_crt_ctx.devs[i].extern_data = __cw_ioctl_devs + i;
        dev_set_drvdata(__dev_crt_ctx.devs[i].device, (__cw_ioctl_devs + i));
    }

    // 创建 sysfile
    for (i = 0; i < __cw_ioctl_nr_devs; i++) {
        device_create_file(__dev_crt_ctx.devs[i].device, &dev_attr_cw_thrdlist);
        device_create_file(__dev_crt_ctx.devs[i].device, &dev_attr_cw_tasklist);
    }
    pr_info(MODULE_TAG " init successfully!\n");
    return 0;

out1:
    module_destroy_cdevs(&__dev_crt_ctx);
    return ret;
}

static void __exit cw_ioctl_test_exit(void)
{
    uint32_t i = 0;
    for (; i < __dev_crt_ctx.count; i++) {
        device_remove_file(__dev_crt_ctx.devs[i].device, &dev_attr_cw_thrdlist);
        device_remove_file(__dev_crt_ctx.devs[i].device, &dev_attr_cw_tasklist);
    }
    module_destroy_cdevs(&__dev_crt_ctx);
    for (i = 0; i < __dev_crt_ctx.count; i++) {
        kfree(__cw_ioctl_devs[i].buf);
    }
    kfree(__cw_ioctl_devs);
    pr_info(MODULE_TAG " bye!\n");
}

module_init(cw_ioctl_test_init);
module_exit(cw_ioctl_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("ioctl test");
MODULE_VERSION("0.1");