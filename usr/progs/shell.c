// shell.c - A basic shell for 391 OS
//
// Copyright (c) 2026 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "../syscall.h"
#include "../string.h"
#include "../shell.h"
#include "../error.h"
#include "../io.h"

// INTERNAL CONSTANT DEFINITIONS
//

#define BUFSIZE 256
#define MAXARGS 64

#define SKIP_SPACES(buf) while(*buf == ' ') buf++
#define RST_IO()                    \
    do {                            \
        _close(STDIN);              \
        _iodup(CONSOLEOUT, STDIN);  \
        _close(STDOUT);             \
        _iodup(CONSOLEOUT, STDOUT); \
    } while (0)

// INTERNAL FUNCTION DECLARATIONS
//
static void __attribute__ ((noreturn)) exec(int argc, char* argv[]);
static void handle_sq(int argc, char* argv[]);
static void handle_bg(int argc, char* argv[]);
static int handle_file_input(char* path);
static int handle_file_output(char* path);
static void handle_pipe(int argc, char* argv[]);
static void __attribute__ ((noreturn)) parse_and_exec(char* head);

static int input_contain_slash(const char *input) {
    return strchr(input, '/') != NULL;
}

// Executes the file in argv[0], passing in argc and argv. If argv[0] is not a
// valid file path, it should prepend '/c/' before executing.
//
// On entry exec() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// This function should not return.
// - on success, executes argv[0], clearing the current memory space.
// - on failure, print an error message and immediately exit.
static void exec(int argc, char* argv[]) {
    // YOUR CODE HERE
    char path[BUFSIZE];
    int fd;
    int result;
    if (argc < 1 || argv[0] == NULL) {
        dprintf(CONSOLEOUT, "exec is missing program\n");
        _exit();
    }

    if (input_contain_slash(argv[0])) {
        snprintf(path, BUFSIZE, "%s", argv[0]);
    } else {
        snprintf(path, BUFSIZE, "/c/%s", argv[0]); // if no '/' detected, assume path is in mountpoint /c
    }

    fd = _open(-1, path);
    if (fd < 0) {
        dprintf(CONSOLEOUT, "exec cannot open %s\n", path);
        _exit();
    }

    result = _exec(fd, argc, argv);

    // exec failed, as exec should not return on success run
    dprintf(CONSOLEOUT, "exec failed to execute %s with error %d\n", path, result);
    _close(fd);
    _exit();
}

// Execs with /argc/ and /argv/, waits for execution to complete, then 
// returns
//
// On entry handle_file_output() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_sq() guarantees (on both success and failure):
// - A process with /argc/ and /argv/ has been exec'd and has completed
//   execution.
static void handle_sq(int argc, char* argv[]) {
    // YOUR CODE HERE
    int child;
    child = _fork();
    if (child < 0) {
        dprintf(CONSOLEOUT, "fork failed\n");
        return;
    }
    // sequential execution, run command in child, then wait for it to return
    if (child == 0) {
        exec(argc, argv);
    }
    _wait(child);
    return;
}

// Execs with /argc/ and /argv/, returns immediately
//
// On entry handle_file_output() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_bg() guarantees (on both success and failure):
// - A process with /argc/ and /argv/ has been exec'd (may not have 
//   finished execution).
static void handle_bg(int argc, char* argv[]) {
    // YOUR CODE HERE
    int child;
    child = _fork();
    if (child < 0) {
        dprintf(CONSOLEOUT, "fork failed\n");
        return;
    }
    // background execution, run child and continue to next command (does not wait)
    if (child == 0) {
        exec(argc, argv);
    }
    return;
}

// Redirects STDIN to /path/.
//
// On entry handle_file_input() assumes:
// - /path/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_file_input() guarantees (on success):
// - STDIN is redirected to the file in /path/.
// - on failure, handle_file_input() returns a negative error code.
static int handle_file_input(char* path) {
    // YOUR CODE HERE
    int result;
    _close(STDIN);
    result = _open(STDIN, path);
    if (result < 0) {
        dprintf(CONSOLEOUT, "handle file input redirection failed: %s\n", path);
        return result;
    }
    return 0;
}

// Redirects STDOUT to /path/. Attemptes to create /path/ if it does not exist.
//
// On entry handle_file_output() assumes:
// - /path/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_file_output() guarantees (on success):
// - /path/ will exist and STDOUT will be redirected to be /path/.
// - on failure, handle_file_output() returns a negative error code.
static int handle_file_output(char* path) {
    // YOUR CODE HERE
    int result;
    _close(STDOUT);
    result = _open(STDOUT, path);
    if (result < 0) {
        // path does not exist, attempt to create path
        result = _create(path);
        if (result < 0) {
            dprintf(CONSOLEOUT, "create failed: %s\n", path);
            return result;
        }
        result = _open(STDOUT, path);
        if (result < 0) {
            dprintf(CONSOLEOUT, "handle file output redirection failed: %s\n", path);
            return result;
        }
    }
    return 0;
}

// Creates a pipe and a reader and writer process. The writer's STDOUT is 
// redirected to the input of the pipe and the reader's STDIN is redirected
// to the output of the pipe. The writer will immediately exec with argc and
// argv, while the reader will return to continue parsing the remainder of the
// input.
//
// On entry handle_pipe() assumes:
// - /argc/ >= 1.
// - /argv[0]/ is non-NULL and points to a NUL-terminated string.
//
// On return handle_pipe() guarantees (on success):
// - The writer's STDOUT is redirected to the input of a newly created pipe and
//   has exec'ed with /argc/ and /argv/.
// - The reader's STDIN is redirected to the input of said pipe and has 
//   returned.
// - on failure, handle_pipe() exits immediately.
static void handle_pipe(int argc, char* argv[]) {
    // YOUR CODE HERE
    int wfd;
    int rfd;
    int child;
    int result;

    // setup pipe
    wfd = -1;
    rfd = -1;
    result = _pipe(&wfd, &rfd);
    if (result < 0) {
        dprintf(CONSOLEOUT, "pipe failed\n");
        _exit();
    }

    child = _fork();
    if (child < 0) {
        dprintf(CONSOLEOUT, "fork failed in handle pipe\n");
        _close(wfd);
        _close(rfd);
        _exit();
    }
    if (child == 0) {
        // fork writer child, redirect stdout to the pipe writer end
        _close(STDOUT); // iodup requires an empty destination fd, close stdout to fulfill
        result = _iodup(wfd, STDOUT);
        if (result < 0) {
            dprintf(CONSOLEOUT, "pipe writer duplication failed\n");
            _exit();
        }
        _close(wfd);
        _close(rfd);
        exec(argc, argv);
    }

    _close(STDIN); // redirect stdin to the pipe reader end
    result = _iodup(rfd, STDIN);
    if (result < 0) {
        dprintf(CONSOLEOUT, "pipe reader duplication failed\n");
        _close(wfd);
        _close(rfd);
        _exit();
    }

    _close(wfd); // close writer end inherited to allow right side command observe EOF when real writer exits
    _close(rfd);
    return;
}

static int is_terminator(char c) {
    switch (c) {
        case ' ':
        case '\0':
        case SQ:
        case BG:
        case FIN:
        case FOUT:
        case PIPE:
            return 1;
        default:
            return 0;
    }
}

static char find_terminator(char* head, char** end) {
    *end = head;
    while (!is_terminator(**end)) (*end)++;
    return **end;
}

static void parse_and_exec(char* head) {
    int argc;
    char* argv[MAXARGS + 1]; // +1 for NULL termination
    char* end;
    char term;
    int res;

    SKIP_SPACES(head);

    // handle args
    for (argc = 0; argc < MAXARGS;) {
        term = find_terminator(head, &end);
        *end = '\0';
        
        if (head != end)
            argv[argc++] = head;
        
        if (term != ' ') break;

        end++;
        SKIP_SPACES(end);
        head = end;
    }

    if (argc == 0) _exit(); // nothing to do

	// Null-terminate the argument array
	argv[argc] = NULL;

    // at this point, anything remaining should be redirection
    while (term != '\0') {
        head = end + 1;
        SKIP_SPACES(head);
        switch (term) {
        case SQ: // run sequentially
            handle_sq(argc, argv);
            // prev cmd has been exec'd and finished
            // reset any redirections and exec the rest
            RST_IO();
            parse_and_exec(head);
            
        case BG: // run in background
            handle_bg(argc, argv);
            // prev cmd has been exec'd, and is running in the background
            // reset any redirections and exec the rest
            RST_IO();
            parse_and_exec(head);

        case FIN: // file input redirection
            term = find_terminator(head, &end);
            *end = '\0';
            res = handle_file_input(head);
            if (res < 0) _exit();
            // we may have more redirection, so we continue
            break;

        case FOUT: // file output redirection
            term = find_terminator(head, &end);
            *end = '\0';
            res = handle_file_output(head);
            if (res < 0) _exit();
            // we may have more redirection, so we continue
            break;

        case PIPE: // set up pipe
            RST_IO(); // any previous redirections are invalid
            handle_pipe(argc, argv);
            // writer has been exec'd and is writing into the pipe
            // now parse and exec reader, who is reading from the pipe
            parse_and_exec(head);
        }

        if (term == ' ') {
            end++;
            SKIP_SPACES(end);
            term = *end;
        }
    }

    exec(argc, argv);
}

void main() {
    char buf[BUFSIZE];
    int child;

    buf[BUFSIZE-1] = '\0'; // terminate

	_open(CONSOLEOUT, "/dev/uart1");    // console device
    RST_IO();

    // Your starting prompt
	printf(/* CHANGE ME */ "Starting Shell...\n");

	for (;;) {
        // Your shell prompt
        // Make sure your prompt ends in one of '>', '#', '$', '%'
		printf(/* CHANGE ME */ "GOGOGO OS> ");
		getsn(buf, BUFSIZE - 1);

		if (0 == strcmp(buf, "exit"))
			return;

        // Now exec the inputted string
        // 
        // For CP3, you should reset the loop 
        // after the exec call has finished
        //
        // YOUR CODE HERE

        // run the parsed user request in a child process
        child = _fork();
        if (child < 0) {
            dprintf(CONSOLEOUT, "fork failed\n");
            RST_IO();
            continue;
        }
        if (child == 0) {
            parse_and_exec(buf);
        }
        _wait(child);
        RST_IO();
	}
}
