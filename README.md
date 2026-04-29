# pgtbl-view — Page Table Viewer / 页表查看器

A kernel driver and Qt GUI application for browsing x86_64 page table entries
in a tree view, expanding each level to reveal all 512 entries of the next
page table.

一套内核驱动与 Qt 图形界面程序，以树形视图逐级浏览 x86_64 页表条目，展开
各级即可显示下级页表的全部 512 条。

---

## 项目结构 / Project Structure

```
pgtbl-view/
├── kernel/                 # 内核模块 / Kernel module
│   ├── pgtbl_dev.c         # 驱动源代码 / Driver source
│   ├── pgtbl_ioctl.h       # ioctl 接口定义 / ioctl interface header
│   ├── Makefile            # 内核模块构建 / Kernel module build
│   └── .gitignore
├── main.cpp                # GUI 入口 / GUI entry point
├── mainwindow.h            # 主窗口声明 / Main window header
├── mainwindow.cpp          # 主窗口实现 / Main window implementation
├── mainwindow.ui           # Qt Designer 界面 / Qt Designer UI layout
├── icons.qrc               # 图标资源 / Icon resources
├── CMakeLists.txt          # GUI 构建 / GUI build system
├── COPYING                 # GPL v2 许可证 / License
└── README.md
```

## 编译方法 / Build Instructions

### 内核模块 / Kernel Module

```bash
cd kernel
make
```

Requires kernel headers for the running kernel.
需要当前运行内核对应的内核头文件。

### GUI 程序 / GUI Application

```bash
cmake -B build -G Ninja
cmake --build build
```

Requires Qt 5 or Qt 6 (Widgets module).
需要 Qt 5 或 Qt 6（Widgets 模块）。

## 使用方法 / Usage

### 1. 加载驱动 / Load the driver

```bash
sudo insmod kernel/pgtbl_dev.ko
```

加载后生成两个设备文件 / Creates two device files:
- `/dev/pgtbl-key` — 仅 root 可读 / root-only, contains the auth key
- `/dev/pgtbl-view` — 任意用户可访问 / accessible by any user

驱动加载时随机生成一个 64 位密钥，保持不变直至卸载。密钥仅可被读取一次。
A random 64-bit key is generated at load time, constant until unload. It can
be read only once.

### 2. 获取密钥 / Obtain the key

`/dev/pgtbl-key` 将密钥以 64 位小端整数原样返回（x86_64 本机字节序）。以下
命令将其转换为大端十六进制字符串，可直接粘贴到 GUI 中：

```bash
sudo hexdump -ve '8/1 "%02x"' /dev/pgtbl-key | fold -w2 | tac | tr -d '\n'
```

`hexdump -e '8/1 "%02x"'` 按内存顺序逐字节输出，`fold -w2 | tac | tr -d '\n'`
反转字节序得到正确的 64 位十六进制表示。
The hexdump outputs bytes in memory order; `fold -w2 | tac | tr -d '\n'`
reverses the byte order to obtain the correct 64-bit hex representation.

### 3. 运行 GUI / Run the GUI

```bash
sudo ./build/pgtbl-view
```

GUI 界面分为三部分：
- **左栏**：页表树。根节点为 CR3，展开后显示全部 512 条 PML4 条目；
  继续展开 PML4/PDPT/PD 条目可查看下级页表全部 512 条。
- **右栏**：选中条目的详细信息 —— Raw 值、物理地址、层级名称、10 个 PTE 标志位。
- **底栏**：提示信息，说明当前条目为何不可展开（非 Present、大页、或已是最终级 PTE）。

操作要点：
- 若以 root 运行，密钥从 `/dev/pgtbl-key` 自动读取并填入（同时消耗密钥）
- 非 root 需手动输入密钥后点击 **Initialize**，再点击 **Refresh** 加载
- 点击条目左侧 `▸` 箭头展开下级页表；每次展开均重新遍历完整路径，页表
  若已被内核更新则提示“页表已更新”并停止展开
- 点击 **Refresh** 按钮重新从 CR3 加载整棵树

The GUI is split into three areas:
- **Left panel**: page table tree. Root node is CR3; expand to see all 512
  PML4 entries. Continue expanding PML4/PDPT/PD entries to reveal the next
  level's full 512-entry table.
- **Right panel**: details of the selected entry — raw value, physical
  address, level name, and 10 PTE flag checkboxes.
- **Bottom bar**: hint explaining why an entry cannot be expanded (not present,
  large page, or terminal PTE).

Key points:
- When run as root, the key is auto-read from `/dev/pgtbl-key` (which
  consumes the key).
- Non-root users must enter the key manually, then click **Initialize** and
  **Refresh**.
- Click the `▸` arrow to expand a level; every expansion re-walks the full
  path. If page tables have been modified in the meantime, the message
  "page table has been updated" is shown and expansion stops.
- Click **Refresh** to reload the entire tree from CR3.

### 4. 卸载驱动 / Unload the driver

```bash
sudo rmmod pgtbl_dev
```

## ioctl 接口 / ioctl Interface

### 单条目查询 / Single Entry Query

```c
struct pgtbl_query {
    __u64 key;      // 认证密钥 / authentication key
    __s32 l1;       // PML4 索引 / PML4 index  [-1..511]
    __s32 l2;       // PDPT 索引 / PDPT index  [-1..511]
    __s32 l3;       // PD 索引   / PD index    [-1..511]
    __s32 l4;       // PT 索引   / PT index    [-1..511]
    __u64 result;   // 输出：CR3 或 PTE 值 / output: CR3 or PTE value
};

#define PGTBL_IOC_QUERY  _IOWR('p', 1, struct pgtbl_query)
```

### 整表查询 / Full Table Query（新增 / new）

```c
#define PGTBL_NENTRIES  512

struct pgtbl_table {
    __u64 key;                      // 认证密钥 / authentication key
    __s32 l1, l2, l3;               // 路径索引 / path indices, -1 terminates
    __u64 parent_entry;             // 输出：路径终点的条目值 / parent entry value
    __u64 entries[PGTBL_NENTRIES];  // 输出：下级页表全部 512 条 / next level table
};

#define PGTBL_IOC_QUERY_TABLE  _IOWR('p', 2, struct pgtbl_table)
```

路径语义 / Path semantics:

| l1 | l2 | l3 | 返回 / Returns |
|----|----|----|---------------|
| -1 | -1 | -1 | `parent_entry`=CR3, `entries`=PML4 表 |
| N  | -1 | -1 | `parent_entry`=PML4E[N], `entries`=PDPT 表 |
| N  | M  | -1 | `parent_entry`=PDPTE[M], `entries`=PD 表 |
| N  | M  | P  | `parent_entry`=PDE[P], `entries`=PT |

每次调用都从 CR3 起重新遍历整个路径；若路径中任一条目变为非 Present 或大页，
返回 `-EFAULT` / `-EINVAL`。

Every call re-walks the full path starting from CR3. If any entry along the
path becomes non-present or a large page, `-EFAULT` / `-EINVAL` is returned.

### 遍历规则 / Traversal Rules

| 条件 / Condition | 行为 / Behavior |
|---|---|
| `l1 == -1` | 返回 CR3 寄存器值 / Return CR3 value |
| 当前 entry 非 Present，且需继续遍历 | 返回 `-EFAULT` |
| 当前 entry 非 Present，不继续遍历 | 返回该 entry 的原值 |
| PDPT/PDE 为大页（PSE=1），需继续遍历 | 返回 `-EINVAL` |
| PDPT/PDE 为大页（PSE=1），不继续遍历 | 返回该 entry 的原值 |
| 密钥不匹配 / Key mismatch | 返回 `-EACCES` |
| 遍历成功 / Traversal succeeds | 返回 0 |

### 页表层级 / Page Table Levels

| 层级 / Level | 名称 / Name | 树中显示 / Tree Label | 大页支持 / Large Page |
|---|---|---|---|
| 0 | CR3 | CR3 | — |
| 1 | PML4E | PML4E | 无 |
| 2 | PDPTE | PDPTE | 1 GB 页 |
| 3 | PDE | PDE | 2 MB 页 |
| 4 | PTE | PTE | 无（终级 / terminal） |

## 条目展开规则 / Entry Expansion Rules

树中条目是否显示 `▸` 展开箭头：

| 层级 | 可展开条件 |
|------|-----------|
| CR3 | 总是（展开至 PML4 表） |
| PML4E | Present == 1 |
| PDPTE | Present == 1 且 PS == 0（非 1GB 大页） |
| PDE | Present == 1 且 PS == 0（非 2MB 大页） |
| PTE | 永远不可展开（终级） |

不可展开时，右侧 Detail 面板会显示原因（大页 / 非 Present / 终级）。

## 页表项标志及树中缩写 / PTE Flags & Tree Abbreviations

| 标志 / Flag | 位 / Bit | 缩写 / Abbr | 含义 / Meaning |
|---|---|---|---|
| Present (P) | 0 | `P` | 页面在内存中 |
| Read/Write (R/W) | 1 | `RW` / `RO` | 读写权限 |
| User (U/S) | 2 | `US` / `S` | 用户态可访问 |
| Write-Through (PWT) | 3 | `WT` | 写穿缓存策略 |
| Cache Disable (PCD) | 4 | `CD` | 禁用缓存 |
| Accessed (A) | 5 | `A` | 已被访问 |
| Dirty (D) | 6 | `D` | 已被写入 |
| Page Size (PS) | 7 | `PS` | 大页（Level 2/3） |
| Global (G) | 8 | `G` | 全局页 |
| Execute Disable (NX) | 63 | `NX` | 禁止执行 |

树中 Flags 列格式 / Tree Flags column format:
`P|RW|US|--|--|A|D|PS|G|NX`
非 Present 条目显示 `-- (not present)`，大页额外标注 `(1GB page)` / `(2MB page)`。

## 技术细节 / Technical Details

- 遍历页表时使用 `preempt_disable()` 禁止抢占，避免 CR3 切换
- `copy_from_kernel_nofault()` 安全读取物理内存，无效地址返回 `-EFAULT`
- 密钥读取使用 `atomic_xchg` 保证并发下仅可读取一次
- GUI 每次展开节点均重新发起 ioctl 完整遍历路径，确保获取最新页表状态
- 树形视图采用懒加载，展开时从内核实时拉取下级页表全部 512 条

- Uses `preempt_disable()` during page table walk to prevent CR3 switch
- `copy_from_kernel_nofault()` for safe physical memory access; invalid
  addresses yield `-EFAULT` instead of kernel panic
- Key reading guarded by `atomic_xchg` to ensure single read even under
  concurrency
- Every tree node expansion re-issues the ioctl to re-walk the full path,
  guaranteeing the latest page table state
- Tree view uses lazy loading: expanding a node fetches all 512 entries of
  the next level from the kernel in real time

## 许可证 / License

pgtbl-view is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation, either version 2 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

The full text of the license is available in the [COPYING](COPYING) file,
or at <https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.

---

Copyright (C) 2025 Bohai Li <lbhlbhlbh2002@icloud.com>
