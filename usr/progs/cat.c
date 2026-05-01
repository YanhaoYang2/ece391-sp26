// YOUR CODE HERE
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"

#define CAT_BUFSZ 128

static void cp_fd_to_stdout(int fd) {
    char buf[CAT_BUFSZ];
    long n;
    long w;
    long offset;

    while (1) {
        n = _read(fd, buf, sizeof(buf));
        if (n < 0) {
            dprintf(CONSOLEOUT, "cat: read error\n");
            _exit();
        }
        if (n == 0) {
            return;
        }
        offset = 0;
        while (offset < n) {
            w = _write(STDOUT, buf + offset, n - offset); // write exactly the bytes read
            if (w <= 0) {
                dprintf(CONSOLEOUT, "cat: write error\n");
                _exit();
            }
            offset += w;
        }
    }
}

void main(int argc, char *argv[]) {
    int fd;
    // no file arguments in input, directly read from stdin
    if (argc == 1) {
        cp_fd_to_stdout(STDIN);
        _exit();
    }
    // find arguments found in input, output all file passed in argv
    for (int i = 1; i < argc; i++) {
        fd = _open(-1, argv[i]);
        if (fd < 0) {
            dprintf(CONSOLEOUT, "cat: cannot open %s\n", argv[i]);
            _exit();
        }
        cp_fd_to_stdout(fd);
        _close(fd);
    }
    _exit();
}
