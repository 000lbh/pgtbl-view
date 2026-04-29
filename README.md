# pgtbl-view — Page Table Viewer / 页表查看器

A kernel driver and Qt GUI application for inspecting x86_64 page table entries
level by level.

一套内核驱动与 Qt 图形界面程序，用于逐级查看 x86_64 页表项。

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
A random 64-bit key is generated at load time, constant until unload. It can be read only once.

### 2. 获取密钥 / Obtain the key

`/dev/pgtbl-key` 将密钥以 64 位小端整数原样返回（x86_64 本机字节序）。以下命令将其转换为大端十六进制字符串，可直接粘贴到 GUI 中：

```bash
sudo hexdump -ve '8/1 "%02x"' /dev/pgtbl-key | fold -w2 | tac | tr -d '\n'
```

`hexdump -e '8/1 "%02x"'` 按内存顺序逐字节输出，`fold -w2 | tac | tr -d '\n'` 反转字节序得到正确的 64 位十六进制表示。
The hexdump outputs bytes in memory order; `fold -w2 | tac | tr -d '\n'` reverses the byte
order to obtain the correct 64-bit hex representation.

### 3. 运行 GUI / Run the GUI

```bash
./build/pgtbl-view
```

- 若以 root 运行，密钥自动从 `/dev/pgtbl-key` 读取
- 若非 root，需手动输入密钥
- 选择各级索引（-1 表示停止遍历），点击 **Query**

- If run as root, the key is auto-read from `/dev/pgtbl-key`
- Otherwise enter the key manually
- Select indices for each level (-1 means stop traversal), click **Query**

### 4. 卸载驱动 / Unload the driver

```bash
sudo rmmod pgtbl_dev
```

## ioctl 接口 / ioctl Interface

```c
struct pgtbl_query {
    __u64 key;      // 认证密钥 / authentication key
    __s32 l1;       // PML4 索引 / PML4 index  [-1..511]
    __s32 l2;       // PDPT 索引 / PDPT index  [-1..511]
    __s32 l3;       // PD 索引   / PD index    [-1..511]
    __s32 l4;       // PT 索引   / PT index    [-1..511]
    __u64 result;   // 输出：CR3 或 PTE 值 / output: CR3 or PTE value
};

// ioctl 命令 / ioctl command
#define PGTBL_IOC_QUERY _IOWR('p', 1, struct pgtbl_query)
```

### 遍历规则 / Traversal Rules

| 条件 / Condition | 行为 / Behavior |
|---|---|
| `l1 == -1` | 返回 CR3 寄存器值 / Return CR3 value |
| 当前 entry 非 Present，且需继续遍历 | 返回 `-EFAULT` |
| 当前 entry 非 Present，不继续遍历 | 返回该 entry 的原值 |
| PDPT/PDE 为大页（PSE=1），需继续遍历 | 返回 `-EINVAL` |
| PDPT/PDE 为大页（PSE=1），不继续遍历 | 返回该 entry 的原值 |
| 密钥不匹配 / Key mismatch | 返回 `-EACCES` |
| 遍历成功 / Traversal succeeds | 返回 0，result 中写入 PTE 值 |

### 页表层级 / Page Table Levels

| 层级 / Level | 名称 / Name | 大页支持 / Large Page |
|---|---|---|
| 0 | CR3 | — |
| 1 | PML4E | 无 |
| 2 | PDPTE | 1 GB 页 |
| 3 | PDE | 2 MB 页 |
| 4 | PTE | 无 |

## 页表项标志 / PTE Flags

GUI 中展示的标志及对应 x86_64 PTE 位 / Flags shown in the GUI:

| 标志 / Flag | 位 / Bit | 含义 / Meaning |
|---|---|---|
| Present (P) | 0 | 页面是否在内存中 |
| Read/Write (R/W) | 1 | 读写权限 |
| User (U/S) | 2 | 用户态可访问 |
| Write-Through (PWT) | 3 | 写穿缓存策略 |
| Cache Disable (PCD) | 4 | 禁用缓存 |
| Accessed (A) | 5 | 已被访问 |
| Dirty (D) | 6 | 已被写入 |
| Page Size (PS) | 7 | 为大页（仅 Level 2/3） |
| Global (G) | 8 | 全局页 |
| Execute Disable (NX) | 63 | 禁止执行 |

## 技术细节 / Technical Details

- 遍历页表时使用 `preempt_disable()` 禁止抢占，避免 CR3 切换导致错误
- `copy_from_kernel_nofault()` 安全读取物理内存，无效地址返回 `-EFAULT` 而非内核崩溃
- 密钥读取使用 `atomic_xchg` 保证并发下仅可读取一次

- Uses `preempt_disable()` during page table walk to prevent CR3 switch
- `copy_from_kernel_nofault()` for safe physical memory access; invalid addresses
  yield `-EFAULT` instead of kernel panic
- Key reading guarded by `atomic_xchg` to ensure single read even under concurrency

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
