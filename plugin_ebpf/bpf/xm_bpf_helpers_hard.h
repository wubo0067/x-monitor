/*
 * @Author: CALM.WU
 * @Date: 2024-03-04 14:55:37
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2024-08-13 14:36:28
 */

#pragma once

#define MINORBITS 20
#define MINORMASK ((1U << MINORBITS) - 1)

#define MAJOR(dev) ((__u32)((dev) >> MINORBITS))
#define MINOR(dev) ((__u32)((dev)&MINORMASK))
#define MKDEV(ma, mi) (((ma) << MINORBITS) | (mi))

// Function to extract block number from an inode structure in the Linux kernel
static __always_inline __s32 __xm_get_blk_num(struct inode *node,
					      struct xm_blk_num *num)
{
	// Read the device number from the inode structure member i_rdev
	dev_t i_rdev;
	BPF_CORE_READ_INTO(&i_rdev, node, i_rdev);
	num->major = MAJOR(i_rdev);
	num->minor = MINOR(i_rdev);
	return 0;
}