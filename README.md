# ECE 391 (SP26) — Operating System Project

**English** | [中文](#中文说明)

Course project for **ECE 391: Computer Systems Engineering**, University of Illinois
Urbana-Champaign, Spring 2026.

The course was a two-stage project:

1. **Individual phase** — MP0 (toolchain/boot), MP1 (RISC-V assembly graphics
   library), MP2 (kernel drivers, interrupts, threads, timers). This phase is
   entirely my own work.
2. **Team phase** — MP3 (file-system cache, processes, syscalls, user mode) and
   the final OS, done in team **team_78 "gogogo"** with `ziyanz6` and `xuzheh2`.

See **[CONTRIBUTIONS.md](CONTRIBUTIONS.md)** for a detailed statement of what I
personally wrote in each phase.

## Branches

| Branch | Contents |
| --- | --- |
| `main` | Final individual submission (MP2 CP4) + this documentation |
| `individual/mp0` | MP0 submission (boot / toolchain) |
| `individual/mp1` | MP1 skyline graphics library in RISC-V assembly |
| `individual/mp2cp1` … `individual/mp2cp4` | MP2 checkpoints CP1–CP4 |
| `team/final` | team_78 final OS (MP3 CP3 / `team_draft`) |
| `team/mp3cp1` … `team/mp3cp3` | Team checkpoints CP1–CP3 |

`main` and the `team/*` branches have unrelated histories: the team repository
started from the course's MP3 skeleton, not from this individual repository. The
team branches are kept as-is so the original commit history and authorship are
preserved.

## Layout (`main`)

```
sys/            RISC-V S-mode kernel
  board/qvirt/  QEMU virt board support, linker script
  dev/          UART, RTC, virtio (block, entropy) drivers
  games/        prebuilt user binaries used by the MP2 tests
  plic.c        PLIC interrupt controller driver
  intr.c        interrupt manager
  thread.c      kernel threads, scheduler, condition variables
  timer.c       timer interrupt and sleep
  io.c          IO object layer (refcount, read/write/ioctl)
  heap.c        kernel heap
```

## Build and run

Toolchain: `riscv64-unknown-elf-gcc`, `qemu-system-riscv64`.

```sh
cd sys
make            # builds kernel.elf
make run        # runs under QEMU with the provided BIOS
make debug      # QEMU with gdbstub on :1234
```

The team branches (`team/*`) additionally contain `usr/` (user land, shell,
games) and `util/fs/` (host-side ngfs image tools).

## Notes

- Everything here was produced for ECE 391 SP26 coursework. The team branches
  contain joint work of team_78 and are archived for reference; the individual
  branches are my own work.
- The repository is private on purpose: course solutions should not be used by
  future students.

---

## 中文说明

**ECE 391：计算机系统工程**（伊利诺伊大学香槟分校，2026 春季学期）课程项目。

项目分两个阶段：

1. **个人阶段** —— MP0（工具链/启动）、MP1（RISC-V 汇编图形库）、MP2（内核驱动、中断、
   线程、定时器），全部由我独立完成。
2. **团队阶段** —— MP3（文件系统缓存、进程、系统调用、用户态）与最终操作系统，由
   **team_78「gogogo」** 完成，成员为 `ziyanz6`、`xuzheh2`、`yanhaoy2`。

我在每个阶段具体写了哪些代码，见 **[CONTRIBUTIONS.md](CONTRIBUTIONS.md)**。

### 分支说明

| 分支 | 内容 |
| --- | --- |
| `main` | 个人最终提交（MP2 CP4）+ 本文档 |
| `individual/mp0` | MP0 提交（启动/工具链） |
| `individual/mp1` | MP1 RISC-V 汇编图形库 |
| `individual/mp2cp1` … `individual/mp2cp4` | MP2 各检查点 |
| `team/final` | team_78 最终操作系统（MP3 CP3 / `team_draft`） |
| `team/mp3cp1` … `team/mp3cp3` | 团队各检查点 |

`main` 与 `team/*` 是两段互不相关的历史：团队仓库从课程提供的 MP3 骨架开始，并非由
个人仓库派生。团队分支原样保留，以保留原始提交记录与作者信息。

### 构建与运行

需要 `riscv64-unknown-elf-gcc` 与 `qemu-system-riscv64`：

```sh
cd sys
make            # 生成 kernel.elf
make run        # 在 QEMU 中运行
make debug      # 带 gdbstub（:1234）
```

### 说明

- 本仓库内容均为 ECE 391 SP26 课程作业。`team/*` 分支是 team_78 的共同成果，仅作存档；
  个人分支是我自己的代码。
- 仓库设为私有，避免后续选课同学直接使用课程答案。
