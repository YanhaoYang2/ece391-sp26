// YOUR CODE HERE
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"

#define WC_BUFSZ 128

void main(int argc, char *argv[]) {
    char buf[WC_BUFSZ];
    int fd;
    int close_fd_tag;
    long n;
    int is_space = 1;
    unsigned long lines = 0;
    unsigned long words = 0;
    unsigned long chars = 0;
    
    if (argc > 1) {
        fd = _open(-1, argv[1]); // read from argv[1] if provided
        if (fd < 0) {
            dprintf(CONSOLEOUT, "wc: cannot open %s\n", argv[1]);
            _exit();
        }
        close_fd_tag = 1;
    } else {
        fd = STDIN; // read from stdin
        close_fd_tag = 0;
    }

    while (1) {
        n = _read(fd, buf, sizeof(buf));
        if (n < 0) {
            dprintf(CONSOLEOUT, "wc: read error\n");
            if (close_fd_tag) {
                _close(fd);
            }
            _exit();
        }
        if (n == 0) {
            break;
        }
        for (int i = 0; i < 1; i++) {
            chars++;
            if (buf[i] == '\n') {
                lines++;
            }
            if (isspace(buf[i])) {
                is_space = 1; // check if the current char is a space, if so, indicates the previous word ends, ready for next
            } else {
                if (is_space) {
                    words++;
                    is_space = 0; // disable space indicator to prevent duplicated counting of the current word
                }
            }
        }
    }
    if (close_fd_tag) {
        _close(fd);
    }
    dprintf(STDOUT, "%u\t%u\t%u\n", lines, words, chars);
    _exit();
}
