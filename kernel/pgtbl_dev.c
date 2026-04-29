/*
 * pgtbl-view — Page Table Viewer
 * Copyright (C) 2025 Bohai Li <lbhlbhlbh2002@icloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/preempt.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/atomic.h>
#include <asm/pgtable.h>
#include <asm/pgtable_types.h>

#include "pgtbl_ioctl.h"

#define PGTBL_PHYS_MASK 0x000ffffffffff000ULL

static u64 secret_key;
static atomic_t key_read_once;

static inline u64 read_cr3(void)
{
	u64 cr3;
	asm volatile("mov %%cr3, %0" : "=r"(cr3) : : "memory");
	return cr3;
}

static inline int safe_read_u64(u64 *addr, u64 *val)
{
	if (copy_from_kernel_nofault(val, addr, sizeof(*val)))
		return -EFAULT;
	return 0;
}

static int key_dev_open(struct inode *inode, struct file *filp)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	return 0;
}

static ssize_t key_dev_read(struct file *filp, char __user *buf, size_t count,
			    loff_t *pos)
{
	if (atomic_xchg(&key_read_once, 1))
		return 0;
	if (count > sizeof(secret_key))
		count = sizeof(secret_key);
	if (copy_to_user(buf, &secret_key, count)) {
		atomic_set(&key_read_once, 0);
		return -EFAULT;
	}
	return count;
}

static const struct file_operations key_dev_fops = {
	.owner = THIS_MODULE,
	.open  = key_dev_open,
	.read  = key_dev_read,
};

static struct miscdevice key_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "pgtbl-key",
	.fops  = &key_dev_fops,
	.mode  = 0400,
};

static int walk_page_table(s32 l1, s32 l2, s32 l3, s32 l4, u64 *result)
{
	u64 cr3, entry, next;
	u64 *table;
	bool present, large;

	if (l1 == -1) {
		*result = read_cr3();
		return 0;
	}

	cr3 = read_cr3() & PGTBL_PHYS_MASK;
	table = (u64 *)__va(cr3);
	if (safe_read_u64(&table[l1], &entry))
		return -EFAULT;

	present = entry & _PAGE_PRESENT;

	if (l2 == -1) {
		*result = entry;
		return 0;
	}

	if (!present)
		return -EFAULT;

	next = entry & PGTBL_PHYS_MASK;
	table = (u64 *)__va(next);
	if (safe_read_u64(&table[l2], &entry))
		return -EFAULT;

	present = entry & _PAGE_PRESENT;
	large   = entry & _PAGE_PSE;

	if (l3 == -1) {
		*result = entry;
		return 0;
	}

	if (!present)
		return -EFAULT;
	if (large)
		return -EINVAL;

	next = entry & PGTBL_PHYS_MASK;
	table = (u64 *)__va(next);
	if (safe_read_u64(&table[l3], &entry))
		return -EFAULT;

	present = entry & _PAGE_PRESENT;
	large   = entry & _PAGE_PSE;

	if (l4 == -1) {
		*result = entry;
		return 0;
	}

	if (!present)
		return -EFAULT;
	if (large)
		return -EINVAL;

	next = entry & PGTBL_PHYS_MASK;
	table = (u64 *)__va(next);
	if (safe_read_u64(&table[l4], &entry))
		return -EFAULT;

	if (l4 == -1) {
		*result = entry;
		return 0;
	}

	*result = entry;
	return 0;
}

static int read_table(s32 l1, s32 l2, s32 l3,
		      u64 *parent_entry, u64 *entries)
{
	u64 cr3, entry, next;
	u64 *table;

	if (l1 == -1) {
		cr3 = read_cr3();
		*parent_entry = cr3;
		table = (u64 *)__va(cr3 & PGTBL_PHYS_MASK);
		if (copy_from_kernel_nofault(entries, table,
					     sizeof(u64) * PGTBL_NENTRIES))
			return -EFAULT;
		return 0;
	}

	if (l1 < 0 || l1 > 511)
		return -EINVAL;

	cr3 = read_cr3() & PGTBL_PHYS_MASK;
	table = (u64 *)__va(cr3);
	if (safe_read_u64(&table[l1], &entry))
		return -EFAULT;

	if (l2 == -1) {
		*parent_entry = entry;
		if (!(entry & _PAGE_PRESENT))
			return -EFAULT;
		next = entry & PGTBL_PHYS_MASK;
		table = (u64 *)__va(next);
		if (copy_from_kernel_nofault(entries, table,
					     sizeof(u64) * PGTBL_NENTRIES))
			return -EFAULT;
		return 0;
	}

	if (l2 < 0 || l2 > 511)
		return -EINVAL;

	if (!(entry & _PAGE_PRESENT))
		return -EFAULT;
	if (entry & _PAGE_PSE)
		return -EINVAL;

	next = entry & PGTBL_PHYS_MASK;
	table = (u64 *)__va(next);
	if (safe_read_u64(&table[l2], &entry))
		return -EFAULT;

	if (l3 == -1) {
		*parent_entry = entry;
		if (!(entry & _PAGE_PRESENT))
			return -EFAULT;
		if (entry & _PAGE_PSE)
			return -EINVAL;
		next = entry & PGTBL_PHYS_MASK;
		table = (u64 *)__va(next);
		if (copy_from_kernel_nofault(entries, table,
					     sizeof(u64) * PGTBL_NENTRIES))
			return -EFAULT;
		return 0;
	}

	if (l3 < 0 || l3 > 511)
		return -EINVAL;

	if (!(entry & _PAGE_PRESENT))
		return -EFAULT;
	if (entry & _PAGE_PSE)
		return -EINVAL;

	next = entry & PGTBL_PHYS_MASK;
	table = (u64 *)__va(next);
	if (safe_read_u64(&table[l3], &entry))
		return -EFAULT;

	*parent_entry = entry;
	if (!(entry & _PAGE_PRESENT))
		return -EFAULT;
	if (entry & _PAGE_PSE)
		return -EINVAL;
	next = entry & PGTBL_PHYS_MASK;
	table = (u64 *)__va(next);
	if (copy_from_kernel_nofault(entries, table,
				     sizeof(u64) * PGTBL_NENTRIES))
		return -EFAULT;
	return 0;
}

static long view_dev_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct pgtbl_query query;
	struct pgtbl_table tbl;
	s32 indices[4];
	int ret;

	switch (cmd) {
	case PGTBL_IOC_QUERY: {
		if (copy_from_user(&query, (void __user *)arg, sizeof(query)))
			return -EFAULT;

		if (query.key != secret_key)
			return -EACCES;

		indices[0] = query.l1;
		indices[1] = query.l2;
		indices[2] = query.l3;
		indices[3] = query.l4;

		if (indices[0] < -1 || indices[0] > 511 ||
		    indices[1] < -1 || indices[1] > 511 ||
		    indices[2] < -1 || indices[2] > 511 ||
		    indices[3] < -1 || indices[3] > 511)
			return -EINVAL;

		if (!current->mm)
			return -EINVAL;

		preempt_disable();
		ret = walk_page_table(indices[0], indices[1], indices[2],
				      indices[3], &query.result);
		preempt_enable();

		if (ret == 0) {
			if (copy_to_user((void __user *)arg, &query,
					 sizeof(query)))
				return -EFAULT;
		}
		return ret;
	}
	case PGTBL_IOC_QUERY_TABLE: {
		if (copy_from_user(&tbl, (void __user *)arg, sizeof(tbl)))
			return -EFAULT;

		if (tbl.key != secret_key)
			return -EACCES;

		if (!current->mm)
			return -EINVAL;

		preempt_disable();
		ret = read_table(tbl.l1, tbl.l2, tbl.l3,
				 &tbl.parent_entry, tbl.entries);
		preempt_enable();

		if (ret == 0) {
			if (copy_to_user((void __user *)arg, &tbl,
					 sizeof(tbl)))
				return -EFAULT;
		}
		return ret;
	}
	default:
		return -ENOTTY;
	}
}

static const struct file_operations view_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = view_dev_ioctl,
};

static struct miscdevice view_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "pgtbl-view",
	.fops  = &view_dev_fops,
	.mode  = 0666,
};

static int __init pgtbl_dev_init(void)
{
	int ret;

	get_random_bytes(&secret_key, sizeof(secret_key));

	ret = misc_register(&key_dev);
	if (ret) {
		pr_err("pgtbl: failed to register pgtbl-key\n");
		return ret;
	}

	ret = misc_register(&view_dev);
	if (ret) {
		pr_err("pgtbl: failed to register pgtbl-view\n");
		misc_deregister(&key_dev);
		return ret;
	}

	return 0;
}

static void __exit pgtbl_dev_exit(void)
{
	misc_deregister(&view_dev);
	misc_deregister(&key_dev);
	pr_info("pgtbl: bye!\n");
}

module_init(pgtbl_dev_init);
module_exit(pgtbl_dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bohai Li");
MODULE_DESCRIPTION("Page table viewer driver");
MODULE_VERSION("0.1");
