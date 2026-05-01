// YOUR CODE HERE
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"

#define LS_BUFSZ 128

void main(int argc, char *argv[]) {
    const char *path;
    char name[LS_BUFSZ + 1];
    int fd;
    int n;
    if (argc > 1) {
        path = argv[1];
    } else {
        path = "/"; // default to root mountpoint if no args passed in
    }
    fd = _open(-1, path);
    if (fd < 0) {
        dprintf(CONSOLEOUT, "ls: connot open %s\n", path);
        _exit();
    }
    while (1) {
        n = _read(fd, name, LS_BUFSZ);
        if (n < 0) {
            dprintf(CONSOLEOUT, "ls: read error\n");
            _close(fd);
            _exit();
        }
        if (n == 0) {
            break;
        }
        name[n] = '\0';
        dprintf(STDOUT, "%s\n", name);
    }
    _close(fd);
    _exit();
}
