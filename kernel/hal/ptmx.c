#include <assert.h>
#include <errno.h>
#include <search.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <termios.h>

#include <kernel/fs/dev.h>
#include <kernel/fs/file.h>
#include <kernel/fs/vfs.h>
#include <kernel/hal/output.h>
#include <kernel/hal/ptmx.h>
#include <kernel/hal/tty.h>
#include <kernel/proc/process.h>
#include <kernel/sched/process_sched.h>
#include <kernel/util/spinlock.h>

#define PTMX_MAX 16

#define TTY_BUF_START 256

#define CONTROL_MASK 0x1F
#define CONTROL_KEY(c) ((c) & CONTROL_MASK)

static struct termios default_termios = {
    ICRNL | IXON,
    OPOST,
    CS8,
    ECHO | ICANON | IEXTEN | ISIG,
    { 
        CONTROL_KEY('d'), '\n', '#', CONTROL_KEY('c'), 
        '@', 0, CONTROL_KEY('\\'), CONTROL_KEY('q'), 
        CONTROL_KEY('s'), CONTROL_KEY('z'), 0
    }
};

static struct device *slaves[PTMX_MAX] = { 0 };
static struct device *masters[PTMX_MAX] = { 0 };
static spinlock_t lock = SPINLOCK_INITIALIZER;

static void slave_on_open(struct device *device) {
    struct slave_data *data = device->private;
    assert(data);

    spin_lock(&data->lock);
    data->ref_count++;
    spin_unlock(&data->lock);
}

static ssize_t slave_read(struct device *device, struct file *file, void *buf, size_t len) {
    (void) file;

    struct slave_data *data = device->private;
    if (get_current_process()->pgid != data->pgid) {
        signal_process_group(get_current_process()->pgid, SIGTTIN);
    }

    spin_lock(&data->lock);
    if (data->input_buffer == NULL) {
        while (data->messages == NULL) {
            spin_unlock(&data->lock);
            yield();
            spin_lock(&data->lock);
        }

        struct tty_buffer_message *message = data->messages;
        data->messages = message == message->next ? NULL : message->next;
        remque(message);

        data->input_buffer = malloc(message->len);
        memcpy(data->input_buffer, message->buf, message->len);
        data->input_buffer_length = data->input_buffer_max = message->len;

        free(message->buf);
        free(message);
    }

    size_t to_copy = MIN(len, data->input_buffer_length - data->input_buffer_index);
    memcpy(buf, data->input_buffer + data->input_buffer_index, to_copy);
    data->input_buffer_index += to_copy;

    if (data->input_buffer_index >= data->input_buffer_length) {
        free(data->input_buffer);
        data->input_buffer = NULL;
        data->input_buffer_index = data->input_buffer_length = data->input_buffer_max = 0;
    }

    spin_unlock(&data->lock);
    return (ssize_t) to_copy;
}

static ssize_t slave_write(struct device *device, struct file *file, const void *buf, size_t len) {
    (void) file;


    struct slave_data *data = device->private;
    if (get_current_process()->pgid != data->pgid && (data->config.c_lflag & TOSTOP)) {
        signal_process_group(get_current_process()->pgid, SIGTTOU);
    }

    if (data->config.c_oflag & OPOST) {
        size_t save_len = len;
        for (size_t i = 0; i < save_len; i++) {
            if (((const char*) buf)[i] == '\n') {
                len++;
            }
        }
    }

    struct master_data *mdata = masters[data->index]->private;
    spin_lock(&mdata->lock);

    struct tty_buffer_message *message = mdata->messages;
    if (message == NULL) {
        message = calloc(1, sizeof(struct tty_buffer_message));
        message->buf = malloc(MAX(TTY_BUF_MAX_START, len));
        message->len = 0;
        message->prev = message->next = message;
        mdata->messages = message;
    } else {
        if (message->max < message->len + len) {
            message->max = MAX(message->max + TTY_BUF_MAX_START, message->len + len);
            message->buf = realloc(message->buf, message->max); 
        }
    }

    size_t buf_index = 0;
    for (size_t i = message->len; i < message->len + len; i++, buf_index++) {
        if ((data->config.c_oflag & OPOST) && ((const char*) buf)[buf_index] == '\n') {
            message->buf[i++] = '\r';
        }
        message->buf[i] = ((const char*) buf)[buf_index];
    }

    message->len += len;

    spin_unlock(&mdata->lock);
    return (ssize_t) len;
}

static int slave_close(struct device *device) {
    struct slave_data *data = device->private;
    assert(data);

    spin_lock(&data->lock);
    data->ref_count--;
    if (data->ref_count <= 0) {
        // data->lock will be unlocked in remove callback
        dev_remove(device->name);
        return 0;
    }

    spin_unlock(&data->lock);
    return 0;
}

static void slave_add(struct device *device) {
    struct slave_data *data = calloc(1, sizeof(struct slave_data));
    init_spinlock(&data->lock);
    data->ref_count = 1; // For the master

    data->cols = VGA_WIDTH;
    data->rows = VGA_HEIGHT;
    data->pgid = get_current_process()->pgid;

    memcpy(&data->config, &default_termios, sizeof(struct termios));

    for (int i = 0; i < PTMX_MAX; i++) {
        if (device == slaves[i]) {
            data->index = i;
            break;
        }
    }

    device->private = data;
}

static void slave_remove(struct device *device) {
    struct slave_data *data = device->private;
    assert(data);

    spin_unlock(&data->lock);


    slaves[data->index] = NULL;

    free(data->input_buffer);

    debug_log("Removing slave tty 1: [ %d ]\n", data->index);

    while (data->messages) {
        struct tty_buffer_message *m = data->messages->next == data->messages ? NULL : data->messages->next;
        remque(data->messages);
        free(data->messages->buf);
        free(data->messages);
        data->messages = m;
    }

    debug_log("Removing slave tty 2: [ %d ]\n", data->index);

    free(data);
}

static int slave_ioctl(struct device *device, unsigned long request, void *argp) {
    if (request == TISATTY) {
        return 0;
    }

    struct slave_data *data = device->private;

    if (get_current_process()->pgid != data->pgid) {
        signal_process_group(get_current_process()->pgid, SIGTTOU);
    }

    struct device *master = masters[data->index];
    struct master_data *mdata = master->private;

    switch (request) {
        case TIOCSWINSZ: {
            struct winsize *w = argp;
            spin_lock(&data->lock);
            data->rows = w->ws_row;
            data->cols = w->ws_col;
            spin_unlock(&data->lock);
            return 0;
        }
        case TIOCGWINSZ: {
            struct winsize *w = argp;
            w->ws_row = data->rows;
            w->ws_col = data->cols;
            return 0;
        }
        case TIOCGPGRP: {
            return data->pgid;
        }
        case TIOCSPGRP: {
            spin_lock(&data->lock);
            data->pgid = *((pid_t*) argp);
            spin_unlock(&data->lock);
            return 0;
        }
        case TCGETS: {
            memcpy(argp, &data->config, sizeof(struct termios));
            return 0;
        }
        case TCSETSF: {
            spin_lock(&mdata->lock);
            free(mdata->input_buffer);
            mdata->input_buffer = NULL;
            mdata->input_buffer_length = 0;
            mdata->input_buffer_max = 0;
            spin_unlock(&mdata->lock);
        } // Fall through
        case TCSETSW: {
            // Should flush output
        } // Fall through
        case TCSETS: {
            spin_lock(&data->lock);
            memcpy(&data->config, argp, sizeof(struct termios));
            spin_unlock(&data->lock);
            return 0;
        }
        case TGETNUM: {
            int *i = argp;
            *i = data->index;
            return 0;
        }
        case TIOSCTTY: {
            get_current_process()->tty = data->index;
            return 0;
        }
        case TIOCNOTTY: {
            get_current_process()->tty = -1;
            return 0;
        }
        default: {
            return -ENOTTY;
        }
    }
}

static struct device_ops slave_ops = {
    NULL, slave_read, slave_write, slave_close, slave_add, slave_remove, slave_ioctl, slave_on_open, NULL
};

static void master_on_open(struct device *device) {
    device->cannot_open = true;
}

static ssize_t master_read(struct device *device, struct file *file, void *buf, size_t len) {
    (void) file;

    struct master_data *data = device->private;

    spin_lock(&data->lock);
    if (data->output_buffer == NULL) {
        if (data->messages == NULL) {
            spin_unlock(&data->lock);
            return 0;
        }

        struct tty_buffer_message *message = data->messages;
        data->messages = message == message->next ? NULL : message->next;
        remque(message);

        data->output_buffer = malloc(message->len);
        data->output_buffer_length = data->output_buffer_max = message->len;
        memcpy(data->output_buffer, message->buf, data->output_buffer_length);

        free(message->buf);
        free(message);
    }

    size_t to_read = MIN(len, data->output_buffer_length - data->output_buffer_index);
    memcpy(buf, data->output_buffer + data->output_buffer_index, to_read);
    data->output_buffer_index += to_read;

    if (data->output_buffer_index >= data->output_buffer_length) {
        free(data->output_buffer);
        data->output_buffer = NULL;
        data->output_buffer_index = data->output_buffer_length = data->output_buffer_max = 0;
    }

    spin_unlock(&data->lock);
    return (ssize_t) to_read;
}

static void tty_do_echo(struct master_data *data, struct slave_data *sdata, char c) {
    if (sdata->config.c_lflag & ECHO) {
        spin_unlock(&data->lock);
        slave_write(slaves[data->index], NULL, &c, 1);
        spin_lock(&data->lock);
    }
}

static bool tty_do_signals(struct slave_data *sdata, char c) {
    if (!(sdata->config.c_lflag & ISIG)) {
        return false;
    }

    if (c == sdata->config.c_cc[VINTR]) {
        signal_process_group(sdata->pgid, SIGINT);
    } else if (c == sdata->config.c_cc[VSUSP]) {
        signal_process_group(sdata->pgid, SIGTSTP);
    } else if (c == sdata->config.c_cc[VQUIT]) {
        signal_process_group(sdata->pgid, SIGQUIT);
    } else {
        return false;
    }

    return true;
}

static ssize_t master_write(struct device *device, struct file *file, const void *buf, size_t len) {
    (void) file;

    struct master_data *data = device->private;
    struct slave_data *sdata = slaves[data->index]->private;

    spin_lock(&data->lock);

    for (size_t i = 0; i < len; i++) {
        if ((sdata->config.c_lflag & ICANON) && data->input_buffer_length >= data->input_buffer_max) {
            data->input_buffer_max += TTY_BUF_MAX_START;
            data->input_buffer = realloc(data->input_buffer, data->input_buffer_max);
        }

        char c = ((const char*) buf)[i];

        if (c == '\r' && sdata->config.c_iflag & ICRNL) {
            c = '\n';
        }

        tty_do_echo(data, sdata, c);

        if (tty_do_signals(sdata, c)) {
            free(data->input_buffer);
            data->input_buffer = NULL;
            data->input_buffer_length = data->input_buffer_max = 0;
            i = 0;
            continue;
        }

        if (!(sdata->config.c_lflag & ICANON)) {
            spin_lock(&sdata->lock);

            if (sdata->messages == NULL) {
                sdata->messages = calloc(1, sizeof(struct tty_buffer_message));
                sdata->messages->buf = malloc(TTY_BUF_MAX_START);
                sdata->messages->len = 1;
                sdata->messages->max = TTY_BUF_MAX_START;
                sdata->messages->buf[0] = c;
                sdata->messages->prev = sdata->messages->next = sdata->messages;
            } else {
                struct tty_buffer_message *m = sdata->messages->prev;

                if (m->len >= m->max) {
                    m->max += TTY_BUF_MAX_START;
                    m->buf = realloc(m->buf, m->max);
                }

                m->buf[m->len++] = c;
            }

            spin_unlock(&sdata->lock);
            continue;
        }

        if (c == sdata->config.c_cc[VEOL]) {
            data->input_buffer[data->input_buffer_length++] = c;

            spin_lock(&sdata->lock);

            struct tty_buffer_message *message = calloc(1, sizeof(struct tty_buffer_message));
            message->len = message->max = data->input_buffer_length;
            message->buf = malloc(message->len);
            memcpy(message->buf, data->input_buffer, message->len);

            if (sdata->messages == NULL) {
                sdata->messages = message->next = message->prev = message;
            } else {
                insque(message, sdata->messages->prev);
            }

            free(data->input_buffer);
            data->input_buffer = NULL;
            data->input_buffer_length = data->input_buffer_max = 0;

            spin_unlock(&sdata->lock);
            continue;
        }

        data->input_buffer[data->input_buffer_length++] = c;
    }

    spin_unlock(&data->lock);
    return (ssize_t) len;
}

static int master_close(struct device *device) {
    dev_remove(device->name);
    return 0;
}

static void master_add(struct device *device) {
    struct master_data *data = calloc(1, sizeof(struct master_data));
    for (int i = 0; i < PTMX_MAX; i++) {
        if (device == masters[i]) {
            data->index = i;
            break;
        }
    }

    device->private = data;
}

static void master_remove(struct device *device) {
    struct master_data *data = device->private;
    assert(data);

    debug_log("Removing master tty: [ %d ]\n", data->index);

    slave_close(slaves[data->index]);

    masters[data->index] = NULL;

    free(data->input_buffer);
    free(data->output_buffer);

    while (data->messages) {
        struct tty_buffer_message *m = data->messages->next == data->messages ? NULL : data->messages->next;
        remque(data->messages);
        free(data->messages->buf);
        free(data->messages);
        data->messages = m;
    }

    free(data);
}

static int master_ioctl(struct device *device, unsigned long request, void *argp) {
    // We're not a real termial, just a controller of one
    if (request == TISATTY) {
        return -ENOTTY;
    }

    struct master_data *data = device->private;
    struct device *slave = slaves[data->index];
    return slave_ioctl(slave, request, argp);
}

static struct device_ops master_ops = {
    NULL, master_read, master_write, master_close, master_add, master_remove, master_ioctl, master_on_open, NULL
};

static struct file *ptmx_open(struct device *device, int flags, int *error) {
    (void) device;

    debug_log("Opening ptmx\n");

    spin_lock(&lock);
    for (int i = 0; i < PTMX_MAX; i++) {
        if (slaves[i] == NULL && masters[i] == NULL) {
            slaves[i] = calloc(1, sizeof(struct device));
            spin_unlock(&lock);

            slaves[i]->device_number = 0x5000 + i;
            snprintf(slaves[i]->name, sizeof(slaves[i]->name - 1), "tty%d", i);
            slaves[i]->ops = &slave_ops;
            slaves[i]->type = S_IFCHR;
            slaves[i]->private = NULL;

            struct device *master = calloc(1, sizeof(struct device));
            master->device_number = 0x10000 + i;
            snprintf(master->name, 7, "mtty%d",i);
            master->ops = &master_ops;
            master->type = S_IFCHR;
            master->private = NULL;
            masters[i] = master;

            dev_add(masters[i], masters[i]->name);
            dev_add(slaves[i], slaves[i]->name);

            char path[16] = { 0 };
            snprintf(path, 15, "/dev/mtty%d", i);
            debug_log("Opening: [ %s ]\n", path);
            return fs_open(path, flags, error);
        }
    }

    spin_unlock(&lock);
    *error = -ENOMEM;
    return NULL;
}

struct device_ops ptmx_ops = {
    ptmx_open, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static struct file *tty_open(struct device *device, int flags, int *error) {
    (void) device;

    int tty_num = get_current_process()->tty;
    if (tty_num == -1) {
        *error = -ENOENT;
        return NULL;
    }

    char path[20] = { 0 };
    snprintf(path, 19, "/dev/tty%d", tty_num);
    debug_log("Redirecting /dev/tty to [ %s ]\n", path);
    return fs_open(path, flags, error);
}

struct device_ops tty_ops = {
    tty_open, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

void init_ptmx() {
    struct device *ptmx_device = calloc(1, sizeof(struct device));
    ptmx_device->device_number = 0x7500;
    strcpy(ptmx_device->name, "ptmx");
    ptmx_device->ops = &ptmx_ops;
    ptmx_device->private = NULL;
    ptmx_device->type = S_IFCHR;

    dev_add(ptmx_device, ptmx_device->name);

    struct device *tty_device = calloc(1, sizeof(struct device));
    tty_device->device_number = 0x2000;
    strcpy(tty_device->name, "tty");
    tty_device->ops = &tty_ops;
    tty_device->private = NULL;
    tty_device->type = S_IFCHR;

    dev_add(tty_device, tty_device->name);
}