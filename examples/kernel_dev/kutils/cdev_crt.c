/*
 * @Author: calmwu
 * @Date: 2024-06-15 22:16:33
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-14 11:16:38
 */

#define pr_fmt(fmt) "%s:%s():%d: " fmt, KBUILD_MODNAME, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/compiler.h>

#include "dev.h"

/**
 * @brief Creates character devices for a module.
 *
 * This function creates character devices for a module using the provided context.
 *
 * @param ctx The context containing the necessary information for creating the character devices.
 * @return 0 on success, or a negative error code on failure.
 */
int32_t module_create_cdevs(struct cw_cdev_crt_ctx *ctx)
{
    int32_t ret = 0;
    uint32_t i, j, k = 0;
    dev_t dev;
    struct device *device;

    if (unlikely(!ctx)) {
        pr_err("Invalid context\n");
        return -EINVAL;
    }

    if (unlikely(0 == ctx->fops)) {
        pr_err("Invalid file operations\n");
        return -EINVAL;
    }

    if (unlikely(0 == ctx->name)) {
        pr_err("Invalid device name\n");
        return -EINVAL;
    }

    // 分配设备号
    if (ctx->major == 0) {
        // 使用动态分配的方式获取主设备号
        // name 会在/sys/devices 目录下找到，例如这里是/proc/devices/virtual/cw_ioctl_dev，在文件/proc/devices 中也可以找到，
        ret = alloc_chrdev_region(&dev, ctx->base_minor, ctx->count, ctx->name);
        if (ret < 0) {
            pr_err("Unable to allocate major number for device:%s\n",
                   ctx->name);
            return ret;
        }
        ctx->major = MAJOR(dev);
    } else {
        // 使用指定的主设备号
        dev = MKDEV(ctx->major, ctx->base_minor);
        ret = register_chrdev_region(dev, ctx->count, ctx->name);
        if (ret < 0) {
            pr_err("Unable to register major number for device:%s\n",
                   ctx->name);
            return ret;
        }
    }
    pr_info("%s: major:%d, count:%d\n", ctx->name, ctx->major, ctx->count);

    // create class, /sys/class
    ctx->cdev_cls = class_create(ctx->owner, ctx->name);
    if (IS_ERR(ctx->cdev_cls)) {
        ret = PTR_ERR(ctx->cdev_cls);
        pr_err("Unable to create %s class:%d\n", ctx->name, ret);
        goto unreg_chrdev;
    }

    // create cdevs array
    ctx->devs = (struct cw_dev *)kmalloc_array(
            ctx->count, sizeof(struct cw_dev), GFP_KERNEL | __GFP_ZERO);
    if (unlikely(!ctx->devs)) {
        ret = -ENOMEM;
        goto unreg_class;
    }

    // 添加设备
    for (i = 0; i < ctx->count; i++) {
        cdev_init(&ctx->devs[i].cdev, ctx->fops);
        ctx->devs[i].cdev.owner = ctx->owner;
        ret = cdev_add(&ctx->devs[i].cdev, dev + i, 1);
        if (ret < 0) {
            pr_err("cdev_add %s%d failed, ret:%d\n", ctx->name, i, ret);
            goto un_cdev_add;
        } else {
            j = i;
            pr_info("cdev_add %s%d success\n", ctx->name, i);
        }
    }
    j = ctx->count;

    // 创建设备 /dev
    for (i = 0; i < ctx->count; i++) {
        ctx->devs[i].devt = dev + i;
        device = device_create(ctx->cdev_cls, NULL, ctx->devs[i].devt,
                               ctx->dev_priv_data, ctx->name_fmt, i);
        if (IS_ERR(device)) {
            ret = PTR_ERR(device);
            pr_err("Unable to create %s%d, ret:%d\n", ctx->name, i, ret);
            goto un_dev_crt;
        } else {
            k = j;
            ctx->devs[i].device = device;
            pr_info("Create %s%d success\n", ctx->name, i);
        }
    }

    pr_info("module_create_cdevs success\n");
    return 0;

un_dev_crt:
    for (i = 0; i < k; i++) {
        device_destroy(ctx->cdev_cls, dev + i);
    }
un_cdev_add:
    for (i = 0; i < j; i++) {
        cdev_del(&ctx->devs[i].cdev);
    }
    kfree(ctx->devs);
unreg_class:
    class_destroy(ctx->cdev_cls);
unreg_chrdev:
    unregister_chrdev_region(dev, ctx->count);
    return ret;
}

//EXPORT_SYMBOL_GPL(module_create_cdevs);

/**
 * @brief Destroys the character devices associated with the given module context.
 *
 * This function is responsible for destroying the character devices that were created
 * using the cw_cdev_crt_ctx structure. It should be called when the module is being
 * unloaded or when the character devices are no longer needed.
 *
 * @param ctx The cw_cdev_crt_ctx structure containing the character devices to be destroyed.
 */
void module_destroy_cdevs(struct cw_cdev_crt_ctx *ctx)
{
    uint32_t i;
    dev_t dev = MKDEV(ctx->major, ctx->base_minor);

    if (unlikely(!ctx)) {
        pr_err("Invalid context\n");
        return;
    }
    // 删除设备
    for (i = 0; i < ctx->count; i++) {
        pr_info("device_destroy dev:'%s%d'\n", ctx->name, i);
        device_destroy(ctx->cdev_cls, dev + i);
    }
    // 删除类
    class_destroy(ctx->cdev_cls);
    // 删除 cdev
    for (i = 0; i < ctx->count; i++) {
        cdev_del(&ctx->devs[i].cdev);
    }
    // 释放设备数组
    kfree(ctx->devs);
    // 释放设备号
    unregister_chrdev_region(dev, ctx->count);
    pr_info("module_destroy_cdevs success\n");
}

//EXPORT_SYMBOL_GPL(module_destroy_cdevs);
