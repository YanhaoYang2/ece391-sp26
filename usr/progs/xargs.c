// YOUR CODE HERE
#include "../syscall.h"
#include "../string.h"
#include "../shell.h"

#define XARGS_BUFSZ 512
#define XARGS_MAXARGS 64

static int input_contain_slash(const char *input) {
    return strchr(input, '/') != 0;
}

void main(int argc, char *argv[]) {
    char input[XARGS_BUFSZ];
    char path[256];
    char *new_argv[XARGS_MAXARGS + 1];
    int new_argc;
    int fd;
    long n;

    if (argc < 2) {
        dprintf(CONSOLEOUT, "xargs: missing command\n");
        _exit();

    }
    long total = 0;
    while (total < XARGS_BUFSZ - 1) {
        n = _read(STDIN, input + total, (XARGS_BUFSZ - 1) - total); // read stdin into one single buffer
        if (n < 0) {
            dprintf(CONSOLEOUT, "xargs: read error\n");
            _exit();
        }
        if (n == 0)
            break;
        total += n;
    }
    input[total] = '\0';
    new_argc = 0;
    new_argv[new_argc++] = argv[1]; // the actual executed program name is in argv[1]
    int next_cluster = 1;
    for (int i = 0; i < total && new_argc < XARGS_MAXARGS; i++) {
        if (input[i] == ' ' || 
            input[i] == '\n' || 
            input[i] == '\t' || 
            input[i] == '\r') 
        {
            input[i] = '\0';
            next_cluster = 1;
        } else if (next_cluster) {
            new_argv[new_argc++] = &input[i]; // items from stdin are now in additional argument list
            next_cluster = 0;
        }
    }
    new_argv[new_argc] = 0;
    // path check, same as shell
    if (input_contain_slash(argv[1]))
        snprintf(path, sizeof(path), "%s", argv[1]);
    else
        snprintf(path, sizeof(path), "/c/%s", argv[1]);
    fd = _open(-1, path);
    if (fd < 0) {
        dprintf(CONSOLEOUT, "xargs: cannot open %s\n", path);
        _exit();
    }
    _exec(fd, new_argc, new_argv);
    dprintf(CONSOLEOUT, "xargs: exec failed\n");
    _close(fd);
    _exit();
}
