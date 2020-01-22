#ifndef OLD_STDIO

#include <assert.h>
#include <bits/lock.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

int fflush_unlocked(FILE *stream) {
    if (!stream) {
        bool any_failed = false;
        __lock(&__file_list_lock);
        FILE *iter = __file_list_head;
        while (iter) {
            __unlock(&iter->__lock);
            if (fflush(iter) == EOF) {
                any_failed = true;
            }
            iter = iter->__next;
        }
        __unlock(&__file_list_lock);
        return any_failed ? -1 : 0;
    }

    if (!(stream->__flags & __STDIO_WRITABLE)) {
        stream->__flags &= ~__STDIO_HAS_UNGETC_CHARACTER;
        if (!(stream->__flags & __STDIO_EOF) && (stream->__position < (off_t) stream->__buffer_length)) {
            if (lseek(stream->__fd, stream->__position - stream->__buffer_length, SEEK_CUR)) {
                return EOF;
            }

            stream->__position = stream->__buffer_length = 0;
            return 0;
        }
        return 0;
    }

    if ((stream->__flags & _IONBF) || stream->__buffer_length == 0) {
        return 0;
    }

    ssize_t ret = write(stream->__fd, stream->__buffer, stream->__buffer_length);
    if (ret < 0) {
        stream->__flags |= __STDIO_ERROR;
        return EOF;
    }

    stream->__position = stream->__buffer_length = 0;
    return 0;
}

#endif /* OLD_STDIO */