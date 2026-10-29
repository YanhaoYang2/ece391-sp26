# Contribution Statement — ECE 391 SP26

**Author:** Yanhao Yang (`yanhaoy2`, GitHub [`YanhaoYang2`](https://github.com/YanhaoYang2))
**Course:** ECE 391 — Computer Systems Engineering, UIUC, Spring 2026
**Team:** team_78 *"gogogo"* — `ziyanz6`, `xuzheh2`, `yanhaoy2`

This statement is reconstructed from the original git history of the two course
repositories (`git log --author=yanhaoy2`, `git blame`). It covers what I wrote,
not what the team as a whole produced.

---

## 1. Individual phase (MP0 – MP2)

The individual repository was seeded with a course skeleton (commits by course
staff). Everything after the seed — **22 commits, +2,117 / −175 lines of code
across 15 files**, plus the two submission files — is mine.

| Checkpoint | When | What I implemented |
| --- | --- | --- |
| MP0 | 2026-01-19 → 01-24 | Filled in `submission.txt` and the academic-integrity statement, fixed typos/trailing whitespace, removed the integrity file after submission. |
| MP1 | 2026-01-21 → 01-30 | Wrote `mp1.s` (~470 lines of RISC-V assembly): the skyline graphics library — `add_star`, `draw_star`, `add_window`, `draw_window`, `draw_beacon`. Passed all provided tests (`1e60b24` "passed all given simple tests"). |
| MP2 CP1 | 2026-02-09 → 02-11 | PLIC driver (`sys/plic.c`): init, per-source enable/disable, claim/finish; UART driver (`sys/dev/uart.c`) with ring-buffer receive; RTC driver (`sys/dev/rtc.c`); interrupt manager (`sys/intr.c`). |
| MP2 CP2 | 2026-02-11 | Virtio entropy driver (`sys/dev/viorng.c`): device attach, open/read/reclaim, ISR; UART fixes. |
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

---

## 2. Team phase (MP3 and final OS, team_78 "gogogo")

The team repository started from the course's MP3 skeleton. In the team phase I
made **21 commits, +1,011 / −222 lines across 11 files**. My main areas were the
buffer cache, the ngfs write path, process management, the syscall layer and
user-mode trap entry.

| Area | Files | +/− | What I implemented |
| --- | --- | --- | --- |
| Buffer cache | `sys/cache.c` | +222 / −73 | Block cache: `create_cache`, block get/release with reference counts, dirty write-back, eviction, flush; null/edge cases; local tests. |
| ngfs write path | `sys/fs/ngfs.c` | +93 / −47 | `ngfs_store` / `ngfs_write` (write path of the on-disk file system). Later refactored by a teammate, so it no longer appears under my name in `git blame`. |
| Process management | `sys/process.c` | +195 / −83 | `process_exec`, `process_exit`, `process_fork`, stack building, `fork_func`. |
| Syscall layer | `sys/syscall.c` | +362 / −13 | Dispatch plus `fork`, `pipe`, `iodup`, `ab1043` (age-from-DOB syscall), `print`, `usleep`, `create`/`delete`, `open`/`close`, `read`/`write`/`ioctl`. |
| User-mode trap entry | `sys/trap.s` | +105 / −1 | `smode_trap_entry_from_umode` — save/restore user context on trap into S-mode. |
| User-mode exceptions | `sys/excp.c` | +19 / −1 | Exception handling for user mode. |
| User-mode interrupts | `sys/intr.c` | +2 | Interrupt path for user mode. |
| Thread fix | `sys/thread.c` | +4 / −4 | `join_thread` memory-free logic. |
| User stubs | `usr/syscall.S`, `usr/syscall.h` | +8 | `_ab1043` and related user-side wrappers. |

### Ownership of the final team code

`git blame` on `team/final` (percentage of current lines last touched by me):

| File | My lines / total | Share |
| --- | --- | --- |
| `sys/syscall.c` | 357 / 525 | 68% |
| `sys/cache.c` | 153 / 216 | 71% |
| `sys/process.c` | 118 / 245 | 48% |
| `sys/trap.s` | 105 / 393 | 27% |
| `sys/excp.c` | 19 / 102 | 19% |
| others | 15 lines total | — |

### Timeline of my team commits

| Date | Work |
| --- | --- |
| 2026-04-02 → 04-03 | Buffer cache (`cache_block`, refcount, locking), `ngfs_store` / `ngfs_write` |
| 2026-04-23 → 04-24 | Process and syscall functions for CP2; user-mode trap entry; user exception/interrupt handling |
| 2026-04-30 → 05-01 | `fork` (process fork + `fork_func`), `pipe`, `ab1043`, `join_thread` fix, user stubs |

---

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
