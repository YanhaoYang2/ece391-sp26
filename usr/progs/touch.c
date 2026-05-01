// YOUR CODE HERE
#include "../syscall.h"
#include "../shell.h"

void main(int argc, char *argv[]) {
    // iterate and create all files specified
    for (int i = 1; i < argc; i++) {
        _create(argv[i]);
    }
    _exit();
}
