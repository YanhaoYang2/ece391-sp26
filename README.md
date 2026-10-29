# ECE 391 (SP26) — Operating System Project

Course project for **ECE 391: Computer Systems Engineering**, University of Illinois
Urbana-Champaign, Spring 2026.

The course was a two-stage project:

1. **Individual phase** — MP0 (toolchain/boot), MP1 (RISC-V assembly graphics
   library), MP2 (kernel drivers, interrupts, threads, timers). This phase is
   entirely my own work.
2. **Team phase** — MP3 (file system cache, processes, syscalls, user mode) and
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
  dev/          UART, RTC, virtio (block, entropy, GPU) drivers
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
