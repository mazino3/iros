#ifndef _UNISTD_H
#define _UNISTD_H 1

#include <sys/types.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#ifdef __cplusplus
extern "C" {
#endif /* cplusplus */

extern char **environ;

void *sbrk(intptr_t increment);
void _exit(int status) __attribute__((__noreturn__));
int execve(const char *file, char *const argv[], char *const envp[]);
pid_t getpid();
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
off_t lseek(int fd, off_t offset, int whence);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void * buf, size_t count);
int close(int fd);

int execv(const char *path, char *const[]);
int execvp(const char *file, char *const argv[]);
int execvpe(const char *file, char *const argv[], char *const envp[]);
pid_t fork();

int isatty(int fd);
int ftruncate(int fd, off_t length);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _UNISTD_H */