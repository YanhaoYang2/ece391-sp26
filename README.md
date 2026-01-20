# ECE 391 MP0

## Instructions
Please refer to the course website for more in depth and thorough instructions! 
This is just a brief overview.

## Build & Run

```bash
# make the executable file
make

# launch!
qemu-system-riscv64 -m 128M -machine virt -bios none -kernel kernel.elf -nographic

# or, utilizing the power of Makefiles (equivalent to above)
make run
```

---

## GDB

GDB will be your most important tool to debug your program in ECE391. Although you may feel more comfortable using `print` statements from your past experience with coding, the scope of `print` statements will be limited in ECE391. Therefore, we want to ensure that all students know how to use GDB to debug their code.

GDB is an external program running parallel to your code that will have access to see the current state of your program. You can step through each line of code using GDB, set breakpoints, print out expressions, check register values, and many more usages for you to discover.

In this MP, we will ask you to find some information about a program using GDB.

### Setup & Attach

1. Create a new file `~/.config/gdb/gdbinit` and add the following line:

   ```text
   set auto-load safe-path /
   ```

   If you have a WFH setup you might need to put it in some other directory. Proceed to run GDB, and you will see a prompt on what you will need to do.

2. Launch **two** terminals and go to the `mp0` directory.

3. In one terminal, start running the code. Check the Makefile to see how this is different with `make run`!  
   *(Hint: search for the `-S` and `-s` flags in the QEMU documentation.)*

   ```bash
   make debug-gdb
   ```

4. In the second terminal, start the GDB tool from the RISC-V toolchain with the file `gdb_main.elf` as your executable.

   ```bash
   riscv64-unknown-elf-gdb gdb_main.elf

   # A GDB console should start
   ```

5. In the GDB console, attach to the QEMU debugging server you started in the other terminal:

   ```text
   (gdb) target remote :1234
   ```

   You should see something like:

   ```text
   Remote debugging using :1234
   ```

### Questions to Answer (fill in `submission.txt`)

For the following steps, you might wish to check the GDB Reference Card on the course website under References to learn about the exact commands.

For some steps, you will be asked to reveal some information about the current program state, which you will fill in the blanks in the `submission.txt` file for submission.

**Because we will ask for the program counter `pc`, you CANNOT modify any code**, otherwise you may obtain the incorrect value.

1. To begin, set a breakpoint at the label/function `get_secret` and at line 28 of `gdb_main.c`. Then continue the program.
2. At this point, the program should be at `get_secret`.  
   **What is the current value of the `a2` register in decimal?**  
   *(Hint: print the expression `$a2` or use `info reg` command.)*  
   Then continue the program again.
3. In the QEMU terminal (not the GDB console), enter your NETID in **all lowercase** and hit enter. This should be before you reach the next breakpoint.
4. At this point, the program should be at line 28 of `gdb_main.c`. Step forward by **3 assembly instructions** (not lines! instructions are NOT the same as lines).  
   **What is the current value of the `pc` register in hexadecimal?**
5. **What is the instruction that `pc` points to?**  
   *(Hint: you can access memory at a specific memory address and format using the `x` command.)*  
   This should be a `jalr` instruction that composes the `call` to `kprintf`.
6. **What is the instruction that `pc-9` points to?**  
   Notice that this is not a valid instruction because `pc-9` is misaligned!
7. **What is the value of the `secret` variable in hexadecimal?**
8. **Using a `sizeof` expression inside GDB, print out the size of the struct `struct my_struct`.**  
   Notice that the struct is padded (the size is NOT 6 bytes).
9. Using an expression inside GDB, cast the variable `secret` as a `struct my_struct`.  
   **What are the values of the `a` and `b` fields?**

When you are done, you should have a completed `submission.txt` file that looks like the below. **These are not the answers.**

```text
$a2=1234
$pc=0x80001234
jalr -1234(ab)
.insn 1, 0x1234
secret=0x1234567812345678
size=1
a=123456789
b="ab"
```

---

## Handin

To get points for this MP, you must complete the following:

- [ ] Have a working RISC-V toolchain and QEMU environment.
- [ ] Follow the GDB practice and fill in the blanks in `submission.txt` in the root of your repository’s `main` branch.
- [ ] Create a file called `academic_integrity.txt` in the root of your repository’s `main` branch, and type in the text from the image below.
- [ ] Push the above two files to the mp0 branch of your GitHub repository before the deadline.

---

## Git: Highly Recommended Knowledge

For all ECE 391 assignments, you'll need to know how to use Git. All repositories will be stored in a University of Illinois organization on GitHub.

Your individual work will use the repository you have created above, while setting up the game. The final group work will use different repositories, also hosted on GitHub.

Review the documentation (or a tutorial of your choice, or check out the video game [here](https://ohmygit.org/)!) to acquaint yourself with (minimally) the usage of the following Git commands:

```bash
git init
git clone
git branch
git remote
git commit
git pull
git push
git merge
git reset
git revert
git diff
git stash
```

---

## Postamble

Note that under new (as of Spring 2025) policy, EWS Labs will clear your home directory prior to the start of a semester. It's always a good idea to backup any sources you wish to keep in your own personal data storage.

If you would like to understand virtual machine organization, how the environment works or have other questions about the course toolchain, visit office hours or ask a member of course staff!

Finally, welcome to ECE 391!