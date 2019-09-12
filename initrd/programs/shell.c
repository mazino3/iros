#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <assert.h>

char *read_line() {
    int sz = 1024;
    int pos = 0;
    char *buffer = malloc(sz);

    for (;;) {
        assert(pos < sz);

        int c = getchar();

        if (c == EOF || c == '\n') {
            buffer[pos] = '\n';
            return buffer;
        }

        buffer[pos++] = c;
    }
}

char **split_line(char *line) {
    int sz = 1024;
    int pos = 0;
    char **tokens = malloc(sz * sizeof(char*));
    char *token;

    token = strtok(line, " \t\r\n\a");

    while (token != NULL) {
        assert(pos < sz);

        tokens[pos++] = token;
        token = strtok(NULL, " \t\r\n\a");
    }

    tokens[pos] = NULL;
    return tokens;
}

int run_program(char **args) {
    pid_t pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            perror("Shell");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Shell");
    } else {
        int status;
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status));
    }

    return 1;
}

int main() {
    for (;;) {
        printf("> ");
        fflush(stdio);

        char *line = read_line();
        char **args = split_line(line);
        int status = run_program(args);

        free(line);
        free(args);

        if (status != 1) {
            break;
        }
    }

    return EXIT_SUCCESS;
}