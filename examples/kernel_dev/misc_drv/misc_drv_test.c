/*
 * @Author: CALM.WU
 * @Date: 2024-07-16 15:57:37
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-14 18:23:35
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
#include <linux/miscdevice.h>
#include <linux/major.h>
#include "../misc.h"

// 如果编译没有代入版本信息
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#else
#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#define MODULE_TAG "Module:[cw_misc_drv]"

static struct cw_miscdev_drv_ctx {
    struct device *dev;
#define MAXBYTES 16
    char secret_buf[MAXBYTES];
} *__ctx;

static int32_t __open_cw_miscdev(struct inode *inode, struct file *filp)
{
    char *buf = kzalloc(PATH_MAX, GFP_KERNEL);

    if (unlikely(!buf))
        return -ENOMEM;
    PRINT_CTX(); // displays process (or atomic) context info

    pr_info(MODULE_TAG " opening \"%s\" now; wrt open file: f_flags = 0x%x\n",
            file_path(filp, buf, PATH_MAX), filp->f_flags);

    kfree(buf);
    return nonseekable_open(inode, filp);
}

// dd if=/dev/cw_miscdev of=readtest bs=4k count=1
// buf：用户地址空间，该地址空间不能直接读写
// count：要读取的字节数
// f_pos：是读的位置相对于文件开头的偏移
static ssize_t __read_cw_miscdev(struct file *filp, char __user *buf,
                                 size_t count, loff_t *f_pos)
{
    char task_comm[TASK_COMM_LEN];
    struct device *dev = __ctx->dev;
    // 得到 secret_buf 的长度
    size_t secret_len = strnlen(__ctx->secret_buf, MAXBYTES);

    PRINT_CTX();
    dev_info(dev, MODULE_TAG " '%s' want to read %zu bytes, offset:%lld\n",
             get_task_comm(task_comm, current), count, *f_pos);

    // 保证用户空间的 buf 至少有 secret_len 长度
    if (count < secret_len) {
        dev_warn(dev,
                 MODULE_TAG " request of bytes:%zu is < secret data len:%zu\n",
                 count, secret_len);
        return -EINVAL;
    }

    if (secret_len == 0) {
        dev_warn(dev, MODULE_TAG " secret data is empty\n");
        return -EINVAL;
    }

    if (*f_pos >= secret_len) {
        dev_info(dev, MODULE_TAG " read offset:%lld >= secret_len:%zu\n",
                 *f_pos, secret_len);
        return 0; // **返回 EOF, 防止无限循环读取
    }

    // 从内核空间拷贝到用户空间
    if (copy_to_user(buf, __ctx->secret_buf, secret_len)) {
        dev_err(dev, MODULE_TAG " failed to copy data to user space\n");
        return -EFAULT;
    }
    // **更新读取的位置，不然每次传入的 f_pos 都是 0
    *f_pos += secret_len;
    dev_info(dev, MODULE_TAG " read %zu bytes\n", secret_len);

    // 返回读取的字节数
    return secret_len;
}

// dmesg -C; dd if=/dev/urandom of=/dev/cw_miscdev bs=4k count=1; dmesg
// !!echo 123456 > /dev/cw_miscdev，count = 7，31 32 33 34 35 36 0a。包括'\n'
static ssize_t __write_cw_miscdev(struct file *filp, const char __user *buf,
                                  size_t count, loff_t *f_pos)
{
    struct device *dev = __ctx->dev;
    char task_comm[TASK_COMM_LEN];
    char *kbuf;
    ssize_t ret;

    PRINT_CTX();

    // 如果要写入的字节数大于 MAXBYTES
    if (unlikely(count > MAXBYTES)) {
        dev_warn(dev, MODULE_TAG " request of bytes:%zu is > max:%d\n", count,
                 MAXBYTES);
        return -EINVAL;
    }

    dev_info(dev, MODULE_TAG " %s to write %zu bytes\n",
             get_task_comm(task_comm, current), count);

    // 动态分配空间，用于 copy_from_user，kvmalloc 可以在有足够连续物理内存时使用 kmalloc，否则使用 vmalloc，从而提高内存分配的效率和灵活性。
    kbuf = kvzalloc(count, GFP_KERNEL);
    if (unlikely(!kbuf)) {
        dev_err(dev, MODULE_TAG " failed to allocate memory\n");
        return -ENOMEM;
    }

    // 会覆盖缓冲区，但是没有写入'\0'
    if (copy_from_user(kbuf, buf, count)) {
        dev_err(dev, MODULE_TAG " failed to copy data from user space\n");
        kvfree(kbuf);
        return -EFAULT;
    }
    // 16 进制输出 user space 的数据
    print_hex_dump_bytes(MODULE_TAG "User buffer: ", DUMP_PREFIX_OFFSET, kbuf,
                         count);
    // !! 当 kbuf 中的数据长度等于 MAXBYTES 时，该函数会返回-E2BIG(7)，因为'\0'无法写入
    ret = strscpy(__ctx->secret_buf, kbuf, MAXBYTES);
    kvfree(kbuf);

    print_hex_dump_bytes(MODULE_TAG "secret_buf: ", DUMP_PREFIX_OFFSET,
                         __ctx->secret_buf, MAXBYTES);

    dev_info(dev, MODULE_TAG " written %zu bytes, ret:%zd\n", count, ret);

    return count;
}

static int32_t __release_cw_miscdev(struct inode *inode, struct file *filp)
{
    PRINT_CTX();

    return 0;
}

static struct file_operations __cw_misc_fops = {
    .open = __open_cw_miscdev,
    .read = __read_cw_miscdev,
    .write = __write_cw_miscdev,
    .release = __release_cw_miscdev,
    .llseek = no_llseek,
    .owner = THIS_MODULE,
};

static struct miscdevice __cw_miscdev = {
    .minor = MISC_DYNAMIC_MINOR, // 动态分配次设备号
    .name = "cw_miscdev", // misc_register 时会在 /dev 下创建 /dev/cw_miscdev, /sys/class/misc and /sys/devices/virtual/misc
    .mode = 0666,
    .fops = &__cw_misc_fops,
};

static int32_t __init cw_miscdrv_init(void)
{
    int32_t ret;
    struct device *dev;

    ret = misc_register(&__cw_miscdev);
    if (ret < 0) {
        pr_err(MODULE_TAG " misc device register failed. ret: %d\n", ret);
        return ret;
    }

    // 获得注册的 device
    dev = __cw_miscdev.this_device;

    pr_info(MODULE_TAG
            " misc device[10:%d], node:[/dev/%s] register success.\n",
            __cw_miscdev.minor, __cw_miscdev.name);
    dev_info(dev, MODULE_TAG " minor:%d", __cw_miscdev.minor);

    __ctx = devm_kzalloc(dev, sizeof(struct cw_miscdev_drv_ctx), GFP_KERNEL);
    if (unlikely(!__ctx)) {
        pr_err(MODULE_TAG " failed to allocate memory for context\n");
        return -ENOMEM;
    }
    __ctx->dev = dev;
    // 警告：优先使用 strscpy 而不是 strlcpy - 见：https://lore.kernel.org/r/CAHk-=wgfRnXz0W3D37d01q3JFkr_i_uTL=V6A6G1oUZcprmkn...
    strscpy(__ctx->secret_buf, "Hello, world!", MAXBYTES);
    dev_dbg(__ctx->dev,
            "A sample print via the dev_dbg(): driver initialized\n");

    pr_info(MODULE_TAG " hello\n");
    return 0;
}

static void __exit cw_miscdrv_exit(void)
{
    misc_deregister(&__cw_miscdev);
    pr_info(MODULE_TAG " byte\n");
}

module_init(cw_miscdrv_init);
module_exit(cw_miscdrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("miscdrv test");
MODULE_VERSION("0.1");
