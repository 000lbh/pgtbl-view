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
#ifndef PGTBL_IOCTL_H
#define PGTBL_IOCTL_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
#endif

struct pgtbl_query {
	__u64 key;
	__s32 l1;
	__s32 l2;
	__s32 l3;
	__s32 l4;
	__u64 result;
};

#define PGTBL_NENTRIES 512

struct pgtbl_table {
	__u64 key;
	__s32 l1;
	__s32 l2;
	__s32 l3;
	__u64 parent_entry;
	__u64 entries[PGTBL_NENTRIES];
};

#define PGTBL_IOC_MAGIC 'p'
#define PGTBL_IOC_QUERY       _IOWR(PGTBL_IOC_MAGIC, 1, struct pgtbl_query)
#define PGTBL_IOC_QUERY_TABLE _IOWR(PGTBL_IOC_MAGIC, 2, struct pgtbl_table)

#endif
