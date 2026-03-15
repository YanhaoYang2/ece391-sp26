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
    return -ENOTSUP;
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
    return -ENOTSUP;
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
	printf(/* CHANGE ME */ "Starting 391 Shell\n");

	for (;;) {
        // Your shell prompt
        // Make sure your prompt ends in one of '>', '#', '$', '%'
		printf(/* CHANGE ME */ "LUMON OS> ");
		getsn(buf, BUFSIZE - 1);

		if (0 == strcmp(buf, "exit"))
			return;

        // Now exec the inputted string
        // 
        // For CP3, you should reset the loop 
        // after the exec call has finished
        //
        // YOUR CODE HERE
	}
}
