// YOUR CODE HERE
#include "../syscall.h"
#include "../shell.h"

void main(int argc, char *argv[]) {
    // iterate and delete all files specified
    for (int i = 1; i < argc; i++) {
        _delete(argv[i]);
    }
    _exit();
}
