/*
 * @Author: CALM.WU
 * @Date: 2024-04-19 17:27:59
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-14 18:06:07
 */

#define pr_fmt(fmt) "%s:%s():%d: " fmt, KBUILD_MODNAME, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/uaccess.h> // for copy_to/from_user()
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include "../kutils/misc.h"

// Interrupt Request number
#define IRQ_NUM 11
// 转换为中断向量表的下表
#define IRQ_NUM_TO_IVT_NUM (IRQ_NUM + 32)

#define CW_IRQ_DEV "cw_irq_trigger"
#define CW_IRQ_PROC_SWITCH "cw_irq_switch"

static dev_t __cw_dev = 0;
static struct cdev __cw_cdev;
static struct class *__cw_cdev_class;
// 中断开关的状态
static uint8_t __cw_irq_switch_state = 0;

static DEFINE_MUTEX(__mutex);

// 中断处理函数
static irqreturn_t __cw_irq_handler(int irq, void *dev_id)
{
    PRINT_CTX();
    pr_info("irq %d triggered\n", irq);
    return IRQ_HANDLED;
}

// 设备文件的处理函数，用来触发中断
static ssize_t __cw_irq_dev_read(struct file *filp, char __user *buf,
                                 size_t count, loff_t *f_pos)
{
    struct irq_desc *desc;

    pr_info("Trigger irq:%d\n", IRQ_NUM);
    // 将 irq 号转换为 irq 描述符
    desc = irq_to_desc(IRQ_NUM);
    if (!desc) {
        return -EINVAL;
    }
    __this_cpu_write(vector_irq[IRQ_NUM_TO_IVT_NUM], desc);
    asm("int $0x2B");
    return 0;
}

static ssize_t __cw_write_irq_switch(struct file *f, const char __user *ubuf,
                                     size_t count, loff_t *off)
{
    unsigned long flags = 0;
    uint8_t kbuf[32] = { 0 };
    int32_t kbuf_size = 0;

    if (count <= 0) {
        pr_err("Invalid input\n");
        return -EINVAL;
    }

    PRINT_CTX();
    // echo 1sdsdfsfdas > /proc/cw_irq_switch
    // kbuf_size: 12, count: 12
    // **这个 count 是包括换行符，没有包含字符串结束符
    kbuf_size = min(count, sizeof(kbuf) - 1);
    pr_info("kbuf_size: %d, count: %zu\n", kbuf_size, count);

    if (mutex_lock_interruptible(&__mutex))
        return -ERESTARTSYS;

    if (copy_from_user(kbuf, ubuf, kbuf_size)) {
        mutex_unlock(&__mutex);
        return -EFAULT;
    }
    // 因为 count 包含了换行符，所以这里还是要加上字符串结束符
    kbuf[kbuf_size] = 0;
    // 如果直接打印 ubuf，会导致 crash
    // 和 pr_debug 一样，需要编译时加上-DDEBUG
    print_hex_dump_bytes("ubuf: ", DUMP_PREFIX_OFFSET, kbuf, kbuf_size);

    // 读取用户态数据，put_user() 和 get_user() 用来从用户空间获取或者向用户空间发送单个值（例如一个 int，char，或者 long）
    //get_user(flag, ubuf);
    if (kbuf[0] == '0') {
        if (__cw_irq_switch_state == 0) {
            // 屏蔽中断
            pr_info("disable_irq irq:%d\n", IRQ_NUM);
            disable_irq(IRQ_NUM);
            __cw_irq_switch_state = 1;
        } else {
            pr_info("enable_irq irq:%d\n", IRQ_NUM);
            // 在开启中断之后，排队的中断会继续执行，只有一个排队的
            // 如果没有屏蔽该中断就调用该函数，内核会告警：Unbalanced enable for IRQ 11，输出堆栈
            // ** 必须和 disable_irq() 成对使用，否则内核会告警
            enable_irq(IRQ_NUM);
            __cw_irq_switch_state = 0;
        }

    } else if (kbuf[0] == '1') {
        // 判断是否在中断禁止状态
        if (irqs_disabled()) {
            pr_info("<-----Current OS Interrupts are disabled\n");
        } else {
            pr_info("<-----Current OS Interrupts are enabled\n");
        }

        if (__cw_irq_switch_state == 0) {
            pr_info("Disable all interrupts for this CPU");
            local_irq_disable();
            // !! 感觉很快 cpu 的中断屏蔽就会被打开
            __cw_irq_switch_state = 1;
        } else {
            pr_info("Enable all interrupts for this CPU");
            local_irq_enable();
            __cw_irq_switch_state = 0;
        }

        // 判断是否在中断禁止状态
        if (irqs_disabled()) {
            pr_info("Interrupts are disabled---->\n");
        } else {
            pr_info("Interrupts are enabled---->\n");
        }

        // 延迟
        ssleep(1);

        // 应该是其他线程调用了 local_irq_enable，导致短暂延迟后，中断被打开
        if (irqs_disabled()) {
            pr_info("After ssleep Interrupts are disabled---->\n");
        } else {
            pr_info("After ssleep Interrupts are enabled---->\n");
        }

    } else if (kbuf[0] == '2') {
        if (__cw_irq_switch_state == 0) {
            pr_info("Disable all interrupts for this CPU and save flags");
            local_irq_save(flags);
            __cw_irq_switch_state = 1;
        } else {
            pr_info("Enable all interrupts for this CPU and restore flags");
            local_irq_restore(flags);
            __cw_irq_switch_state = 0;
        }

    } else {
        pr_err("Invalid input\n");
    }
    mutex_unlock(&__mutex);
    return kbuf_size;
}

// proc 文件，开关中断
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
static struct file_operations __cw_fops_irq_switch = {
    .owner = THIS_MODULE,
    .write = __cw_write_irq_switch,
};
#else
static struct proc_fops __cw_fops_irq_switch = {
    .proc_write = __cw_write_irq_switch,
};
#endif

// /dev/cw_irq_test文件的读取，触发中断
static struct file_operations __cw_irq_dev_fops = {
    .owner = THIS_MODULE,
    .read = __cw_irq_dev_read,
};

static int32_t __init __cw_irq_test_init(void)
{
    // Allocate a major number dynamically
    if (unlikely(alloc_chrdev_region(&__cw_dev, 0, 1, CW_IRQ_DEV) < 0)) {
        pr_err("Cannot allocate major number\n");
        return -1;
    }
    pr_info("major: %d, minor: %d\n", MAJOR(__cw_dev), MINOR(__cw_dev));
    // Initialize the cdev structure
    cdev_init(&__cw_cdev, &__cw_irq_dev_fops);

    // Add character device to the system
    if (unlikely(cdev_add(&__cw_cdev, __cw_dev, 1) < 0)) {
        pr_err("Cannot add the device to the system\n");
        goto un_class;
    }

    // creating struct class
    __cw_cdev_class = class_create(THIS_MODULE, CW_IRQ_DEV);
    if (unlikely(IS_ERR(__cw_cdev_class))) {
        pr_err("Cannot create the struct class\n");
        goto un_class;
    }

    // creating device
    if (unlikely(IS_ERR(device_create(__cw_cdev_class, NULL, __cw_dev, NULL,
                                      CW_IRQ_DEV)))) {
        pr_err("Cannot create the Device\n");
        goto un_device;
    }

    //注册一个 IRQ，最后一个参数是 ISR 的第二个参数
    if (request_irq(IRQ_NUM, __cw_irq_handler, IRQF_SHARED, CW_IRQ_DEV,
                    (void *)(__cw_irq_handler))) {
        pr_err("Cannot register IRQ\n");
        goto un_irq;
    }
    // 创建中断控制文件/proc/cw_irq_swtch
    if (!proc_create(CW_IRQ_PROC_SWITCH, 0200, NULL, &__cw_fops_irq_switch)) {
        pr_err("Cannot create proc file:'%s'\n", CW_IRQ_PROC_SWITCH);
        goto un_irq;
    }

    pr_info("Hello, irq test module\n");
    return 0;

un_irq:
    free_irq(IRQ_NUM, (void *)(__cw_irq_handler));

un_device:
    class_destroy(__cw_cdev_class);

un_class:
    unregister_chrdev_region(__cw_dev, 1);
    cdev_del(&__cw_cdev);
    return -1;
}

static void __exit __cw_irq_test_exit(void)
{
    free_irq(IRQ_NUM, (void *)(__cw_irq_handler));
    device_destroy(__cw_cdev_class, __cw_dev);
    remove_proc_entry(CW_IRQ_PROC_SWITCH, NULL);
    class_destroy(__cw_cdev_class);
    cdev_del(&__cw_cdev);
    unregister_chrdev_region(__cw_dev, 1);
    pr_info("Goodbye, irq test module\n");
}

module_init(__cw_irq_test_init);
module_exit(__cw_irq_test_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("calmwu <wubo0067@hotmail.com>");
MODULE_DESCRIPTION("delay function Example");
MODULE_VERSION("0.1");

/*
[Sat May  4 22:47:32 2024] interrupt_test:__cw_irq_test_init():65: major: 242, minor: 0
[Sat May  4 22:47:32 2024] interrupt_test:__cw_irq_test_init():96: Hello, irq test module
[Sat May  4 22:49:06 2024] interrupt_test:__cw_irq_dev_read():42: Trigger irq
[Sat May  4 22:49:06 2024] interrupt_test:__cw_irq_handler():32: irq 11 triggered
*/