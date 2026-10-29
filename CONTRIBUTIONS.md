# 贡献说明 — ECE 391 SP26

**作者：** Yanhao Yang（`yanhaoy2`，GitHub [`YanhaoYang2`](https://github.com/YanhaoYang2)）
**课程：** ECE 391 — Computer Systems Engineering，UIUC，2026 春季
**团队：** team_78 *"gogogo"* — `ziyanz6`、`xuzheh2`、`yanhaoy2`

本文根据两个课程仓库的原始 git 记录重建（`git log --author=yanhaoy2`、`git blame`），
只列出**我本人**写的部分，不代表团队整体产出。

> **范围：** 从 MP0 到 MP2 CP4（含 `mp2cp4`）完全由我独立完成；`gogogo` 的
> MP3 CP1–CP3 与最终 OS 属于 team_78 的团队工作。

---

## 一、个人阶段（MP0 – MP2）

个人仓库由课程骨架初始化（骨架提交由课程 staff 完成）。骨架之后的所有提交都是我的：
**22 次提交，代码 +2,117 / −175 行，涉及 15 个文件**（另有 2 个提交说明文件）。

| 检查点 | 时间 | 我实现的内容 |
| --- | --- | --- |
| MP0 | 2026-01-19 → 01-24 | 填写 `submission.txt` 与学术诚信声明，修正 typo/行尾空格，提交后按要求删除诚信文件。 |
| MP1 | 2026-01-21 → 01-30 | 编写 `mp1.s`（约 470 行 RISC-V 汇编）：skyline 图形库——`add_star`、`draw_star`、`add_window`、`draw_window`、`draw_beacon`。通过全部提供的测试（`1e60b24` "passed all given simple tests"）。 |
| MP2 CP1 | 2026-02-09 → 02-11 | PLIC 驱动（`sys/plic.c`）：初始化、按源使能/关闭、claim/finish；UART 驱动（`sys/dev/uart.c`）含环形缓冲接收；RTC 驱动（`sys/dev/rtc.c`）；中断管理（`sys/intr.c`）。 |
| MP2 CP2 | 2026-02-11 | Virtio 熵源驱动（`sys/dev/viorng.c`）：设备 attach、open/read/reclaim、ISR；UART 修正。 |
| MP2 CP3 | 2026-03-02 → 03-06 | 内核线程（`sys/thread.c`、`sys/thrasm.s`）：线程创建、上下文切换、调度、条件变量、`join_thread`；`uart_init` 修正与注释。 |
| MP2 CP4 | 2026-03-09 → 03-15 | Virtio 块设备驱动（`sys/dev/vioblk.c`）：attach、open、ioctl、ISR、块 I/O；memio/IO 层（`sys/io.c`、`sys/io.h`、`sys/ioimpl.h`）；定时器与 sleep（`sys/timer.c`）；补充注释。 |

### 个人代码量（按模块）

| 模块 | 文件 | 新增行数 |
| --- | --- | --- |
| Skyline 图形库（MP1） | `mp1.s` | 470 |
| 内核线程 | `sys/thread.c`、`sys/thread.h`、`sys/thrasm.s` | 483 |
| UART 驱动 | `sys/dev/uart.c` | 320 |
| Virtio 熵源 | `sys/dev/viorng.c` | 243 |
| Virtio 块设备 | `sys/dev/vioblk.c` | 204 |
| IO 对象层 | `sys/io.c`、`sys/io.h`、`sys/ioimpl.h` | 116 |
| PLIC 驱动 | `sys/plic.c` | 92 |
| RTC 驱动 | `sys/dev/rtc.c` | 75 |
| 定时器 / sleep | `sys/timer.c`、`sys/timer.h` | 67 |
| 中断管理 | `sys/intr.c` | 47 |

---

## 二、团队阶段（MP3 与最终 OS，team_78 "gogogo"）

团队仓库从课程 MP3 骨架开始。我在团队阶段共有 **21 次提交，+1,011 / −222 行，涉及 11 个文件**。
主要工作集中在缓冲区缓存、ngfs 写路径、进程管理、系统调用层和用户态 trap 入口。

| 模块 | 文件 | +/− | 我实现的内容 |
| --- | --- | --- | --- |
| 缓冲区缓存 | `sys/cache.c` | +222 / −73 | 块缓存：`create_cache`、块获取/释放与引用计数、脏块回写、淘汰、flush；空指针/边界情况；本地测试。 |
| ngfs 写路径 | `sys/fs/ngfs.c` | +93 / −47 | `ngfs_store` / `ngfs_write`（磁盘文件系统写路径）。后被队友重构，因此最终 `git blame` 里已不在我名下。 |
| 进程管理 | `sys/process.c` | +195 / −83 | `process_exec`、`process_exit`、`process_fork`、栈构建、`fork_func`。除 127 行课程骨架外，该文件全部由我编写。 |
| 系统调用层 | `sys/syscall.c` | +362 / −13 | 系统调用分发，以及 `fork`、`pipe`、`iodup`、`ab1043`（按出生日期算年龄的课程自定义调用）、`print`、`usleep`、`create`/`delete`、`open`/`close`、`read`/`write`/`ioctl`。 |
| 用户态 trap 入口 | `sys/trap.s` | +105 / −1 | `smode_trap_entry_from_umode`：陷入 S 模式时保存/恢复用户上下文。 |
| 用户态异常 | `sys/excp.c` | +19 / −1 | 用户态异常处理。 |
| 用户态中断 | `sys/intr.c` | +2 | 用户态中断路径。 |
| 线程修复 | `sys/thread.c` | +4 / −4 | `join_thread` 的内存释放逻辑。 |
| 用户态桩函数 | `usr/syscall.S`、`usr/syscall.h` | +8 | `_ab1043` 等用户侧封装。 |

### 我在团队阶段的提交时间线

| 日期 | 工作 |
| --- | --- |
| 2026-04-02 → 04-03 | 缓冲区缓存（`cache_block`、引用计数、加锁）、`ngfs_store` / `ngfs_write` |
| 2026-04-23 → 04-24 | CP2 的进程与系统调用函数；用户态 trap 入口；用户态异常/中断处理 |
| 2026-04-30 → 05-01 | `fork`（`process_fork` + `fork_func`）、`pipe`、`ab1043`、`join_thread` 修复、用户态桩函数 |

---

## 三、如何验证

```sh
# 个人部分：我的提交
git log --all --author=yanhaoy2 --oneline --date=short --format='%h %ad %s'

# 个人部分：每个文件我改了多少行
git log --all --author=yanhaoy2 --numstat --format=''

# 团队部分：我的提交与文件
git log team/final --author=yanhaoy2 --oneline --date=short --format='%h %ad %s'
git log team/final --author=yanhaoy2 --name-only --format='' | sort -u

# 团队部分：最终代码归属
git blame team/final -- sys/syscall.c
git blame team/final -- sys/cache.c
```

团队仓库历史中还有 `ziyanz6`、`xuzheh2` 的提交；课程 staff（`ahang5@illinois.edu`）的提交
是骨架/发布提交，不计入团队工作。

---

# Contribution Statement — ECE 391 SP26 (English)

**Author:** Yanhao Yang (`yanhaoy2`, GitHub [`YanhaoYang2`](https://github.com/YanhaoYang2))
**Course:** ECE 391 — Computer Systems Engineering, UIUC, Spring 2026
**Team:** team_78 *"gogogo"* — `ziyanz6`, `xuzheh2`, `yanhaoy2`

This statement is reconstructed from the original git history of the two course
repositories (`git log --author=yanhaoy2`, `git blame`). It covers what I wrote,
not what the team as a whole produced.

> **Scope:** everything from MP0 through MP2 CP4 (inclusive) is my own solo work;
> the `gogogo` MP3 checkpoints and the final OS are team work of team_78.

## 1. Individual phase (MP0 – MP2)

The individual repository was seeded with a course skeleton (commits by course
staff). Everything after the seed — **22 commits, +2,117 / −175 lines of code
across 15 files**, plus the two submission files — is mine.

| Checkpoint | When | What I implemented |
| --- | --- | --- |
| MP0 | 2026-01-19 → 01-24 | Filled in `submission.txt` and the academic-integrity statement, fixed typos/trailing whitespace, removed the integrity file after submission. |
| MP1 | 2026-01-21 → 01-30 | Wrote `mp1.s` (~470 lines of RISC-V assembly): the skyline graphics library — `add_star`, `draw_star`, `add_window`, `draw_window`, `draw_beacon`. Passed all provided tests (`1e60b24` "passed all given simple tests"). |
| MP2 CP1 | 2026-02-09 → 02-11 | PLIC driver (`sys/plic.c`): init, per-source enable/disable, claim/finish; UART driver (`sys/dev/uart.c`) with ring-buffer receive; RTC driver (`sys/dev/rtc.c`); interrupt manager (`sys/intr.c`). |
| MP2 CP2 | 2026-02-11 | Virtio entropy driver (`sys/dev/viorng.c`): attach, open/read/reclaim, ISR; UART fixes. |
| MP2 CP3 | 2026-03-02 → 03-06 | Kernel threads (`sys/thread.c`, `sys/thrasm.s`): thread creation, context switch, scheduler, condition variables, `join_thread`; `uart_init` fixes and comments. |
| MP2 CP4 | 2026-03-09 → 03-15 | Virtio block driver (`sys/dev/vioblk.c`): attach, open, ioctl, ISR, block I/O; memio/IO layer work (`sys/io.c`, `sys/io.h`, `sys/ioimpl.h`); timer driver + sleep (`sys/timer.c`); final comments. |

### Code volume by component (individual)

| Component | Files | Lines added |
| --- | --- | --- |
| Skyline graphics (MP1) | `mp1.s` | 470 |
| Kernel threads | `sys/thread.c`, `sys/thread.h`, `sys/thrasm.s` | 483 |
| UART driver | `sys/dev/uart.c` | 320 |
| Virtio entropy | `sys/dev/viorng.c` | 243 |
| Virtio block | `sys/dev/vioblk.c` | 204 |
| IO object layer | `sys/io.c`, `sys/io.h`, `sys/ioimpl.h` | 116 |
| PLIC driver | `sys/plic.c` | 92 |
| RTC driver | `sys/dev/rtc.c` | 75 |
| Timer / sleep | `sys/timer.c`, `sys/timer.h` | 67 |
| Interrupt manager | `sys/intr.c` | 47 |

## 2. Team phase (MP3 and final OS, team_78 "gogogo")

The team repository started from the course's MP3 skeleton. In the team phase I
made **21 commits, +1,011 / −222 lines across 11 files**. My main areas were the
buffer cache, the ngfs write path, process management, the syscall layer and
user-mode trap entry.

| Area | Files | +/− | What I implemented |
| --- | --- | --- | --- |
| Buffer cache | `sys/cache.c` | +222 / −73 | Block cache: `create_cache`, block get/release with reference counts, dirty write-back, eviction, flush; null/edge cases; local tests. |
| ngfs write path | `sys/fs/ngfs.c` | +93 / −47 | `ngfs_store` / `ngfs_write` (write path of the on-disk file system). Later refactored by a teammate, so it no longer appears under my name in `git blame`. |
| Process management | `sys/process.c` | +195 / −83 | `process_exec`, `process_exit`, `process_fork`, stack building, `fork_func`. Except for 127 lines of course skeleton, the whole file is mine. |
| Syscall layer | `sys/syscall.c` | +362 / −13 | Dispatch plus `fork`, `pipe`, `iodup`, `ab1043` (age-from-DOB syscall), `print`, `usleep`, `create`/`delete`, `open`/`close`, `read`/`write`/`ioctl`. |
| User-mode trap entry | `sys/trap.s` | +105 / −1 | `smode_trap_entry_from_umode` — save/restore user context on trap into S-mode. |
| User-mode exceptions | `sys/excp.c` | +19 / −1 | Exception handling for user mode. |
| User-mode interrupts | `sys/intr.c` | +2 | Interrupt path for user mode. |
| Thread fix | `sys/thread.c` | +4 / −4 | `join_thread` memory-free logic. |
| User stubs | `usr/syscall.S`, `usr/syscall.h` | +8 | `_ab1043` and related user-side wrappers. |

### Timeline of my team commits

| Date | Work |
| --- | --- |
| 2026-04-02 → 04-03 | Buffer cache (`cache_block`, refcount, locking), `ngfs_store` / `ngfs_write` |
| 2026-04-23 → 04-24 | Process and syscall functions for CP2; user-mode trap entry; user exception/interrupt handling |
| 2026-04-30 → 05-01 | `fork` (process fork + `fork_func`), `pipe`, `ab1043`, `join_thread` fix, user stubs |

## 3. How to verify

```sh
# Individual: my commits only
git log --all --author=yanhaoy2 --oneline --date=short --format='%h %ad %s'

# Individual: my changed lines per file
git log --all --author=yanhaoy2 --numstat --format=''

# Team: my commits and files
git log team/final --author=yanhaoy2 --oneline --date=short --format='%h %ad %s'
git log team/final --author=yanhaoy2 --name-only --format='' | sort -u

# Team: ownership of the final tree
git blame team/final -- sys/syscall.c
git blame team/final -- sys/cache.c
```

The team repository's own history also contains commits by `ziyanz6` and
`xuzheh2`; commits by course staff (`ahang5@illinois.edu`) are skeleton/release
commits and are not counted as team work.
