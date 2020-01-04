#ifndef _WORDEXP_H
#define _WORDEXP_H 1

#ifndef USERLAND_NATIVE
#include <bits/size_t.h>
#else
typedef unsigned long size_t;
#endif /* USERLAND_NATIVE */

#define WRDE_APPEND  1
#define WRDE_DOOFFS  2
#define WRDE_NOCMD   4
#define WRDE_REUSE   8
#define WRDE_SHOWERR 16
#define WRDE_UNDEF   32

#define WRDE_BADCHAR -1
#define WRDE_BADVAL  -2
#define WRDE_CMDSUB  -3
#define WRDE_NOSPACE -4
#define WRDE_SYNTAX  -5

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef _OS_2_SOURCE

#define WRDE_SPECIAL 64
#define WRDE_NOFS    128
#define WRDE_NOGLOB  256

#define WRDE_SPECIAL_STAR   0
#define WRDE_SPECIAL_QUEST  1
#define WRDE_SPECIAL_MINUS  2
#define WRDE_SPECIAL_DOLLAR 3
#define WRDE_SPECIAL_EXCLAM 4
#define WRDE_SPECIAL_ZERO   5
#define WRDE_NUM_SPECIAL    6

typedef struct {
    char *vals[WRDE_NUM_SPECIAL];
    char **position_args;
    size_t position_args_size;
} word_special_t;

#endif /* _OS_2_SOURCE */

typedef struct {
    size_t we_wordc;
    char **we_wordv;
    size_t we_offs;
#ifdef _OS_2_SOURCE
    word_special_t *we_special_vars;
#endif /* _OS_2_SOURCE */
} wordexp_t;

int wordexp(const char *s, wordexp_t *p, int flags);
void wordfree(wordexp_t *p);

#ifdef _OS_2_SOURCE
size_t we_find_end_of_word_expansion(const char *input_stream, size_t start, size_t input_length);
int we_add(char *s, wordexp_t *we);
int we_insert(char **arr, size_t arr_size, size_t pos, wordexp_t *we);
int we_expand(const char *s, int flags, char **result, word_special_t *special);
int we_unescape(char **s);
#endif /* _OS_2_SOURCE */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WORDEXP_H */