/*
 * @Author: calmwu
 * @Date: 2024-06-15 22:16:08
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-10-14 16:44:40
 */

#ifndef __CW_KUILTS_H
#define __CW_KUILTS_H

struct file_operations;
struct module;
struct class;
struct device;

struct cw_dev {
    struct cdev cdev;      //
    struct device *device; //
    dev_t devt;            //
    void *extern_data;     //
};

// 字符设备的创建上下文
struct cw_cdev_crt_ctx {
    int32_t major; /*主设备号，如果为 0，表明需要动态分配*/
    uint32_t base_minor;          /*次设备号的起始值*/
    uint32_t count;               /*次设备号的数量*/
    const char *name;             /**/
    const char *name_fmt;         /*名字模板*/
    struct file_operations *fops; /* operations */
    struct module *owner;         // 模块
    void *dev_priv_data;          // 设备私有数据
    struct class *cdev_cls;       //
    struct cw_dev *devs;          //
};

int32_t module_create_cdevs(struct cw_cdev_crt_ctx *ctx);
void module_destroy_cdevs(struct cw_cdev_crt_ctx *ctx);

int32_t show_tasklist(char *buf, size_t buf_size);
int32_t show_thrdlist(char *buf, size_t buf_size);

#endif /* __CW_KUILTS_H */
