#include <eventloop/file_watcher.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __iros__
#include <sys/socket.h>
#include <sys/umessage.h>

namespace App {
FileWatcher::FileWatcher() {
    int watcher_fd = socket(AF_UMESSAGE, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, UMESSAGE_WATCH);
    set_fd(watcher_fd);
    set_selected_events(NotifyWhen::Readable);
    enable_notifications();
}

void FileWatcher::initialize() {
    on<ReadableEvent>([this](auto&) {
        char buffer[1024];

        ssize_t ret;
        while ((ret = read(fd(), buffer, sizeof(buffer))) > 0) {
            auto* message = (umessage*) buffer;
            switch (message->type) {
                case UMESSAGE_WATCH_INODE_MODIFIED: {
                    auto& event = *(umessage_watch_inode_modified*) message;
                    auto path = m_identifier_to_path.get(event.identifier);
                    if (!path) {
                        continue;
                    }

                    emit<PathChangeEvent>(*path);
                    break;
                }
                case UMESSAGE_WATCH_INODE_REMOVED:
                    auto& event = *(umessage_watch_inode_removed*) message;
                    auto path_p = m_identifier_to_path.get(event.identifier);
                    if (!path_p) {
                        continue;
                    }
                    auto path = *path_p;
                    unwatch(path);
                    emit<PathRemovedEvent>(path);
                    break;
            }
        }
    });
}

FileWatcher::~FileWatcher() {
    if (valid()) {
        close(fd());
    }
}

bool FileWatcher::watch(const String& path) {
    if (!valid() || path.size() >= PATH_MAX) {
        return false;
    }

    char buffer[UMESSAGE_WATCH_ADD_PATH_REQUEST_LENGTH(PATH_MAX)];
    auto* request = (umessage_watch_add_path_request*) buffer;

    size_t path_length = path.size() + 1;

    request->base.length = UMESSAGE_WATCH_ADD_PATH_REQUEST_LENGTH(path_length);
    request->base.category = UMESSAGE_WATCH;
    request->base.type = UMESSAGE_WATCH_ADD_PATH_REQUEST;

    request->identifier = m_identifier_index++;
    request->path_length = path_length;
    strcpy(request->path, path.string());

    if (write(fd(), buffer, request->base.length) < 0) {
        return false;
    }

    m_identifier_to_path.put(request->identifier, path);
    m_path_to_indentifier.put(path, request->identifier);
    return true;
}

bool FileWatcher::unwatch(const String& path) {
    auto identifier_p = m_path_to_indentifier.get(path);
    if (!identifier_p) {
        return false;
    }
    auto identifier = *identifier_p;

    m_path_to_indentifier.remove(path);
    m_identifier_to_path.remove(identifier);

    umessage_watch_remove_watch_request request;

    request.base.length = sizeof(request);
    request.base.category = UMESSAGE_WATCH;
    request.base.type = UMESSAGE_WATCH_REMOVE_WATCH_REQUEST;

    request.identifier = identifier;

    if (write(fd(), &request, request.base.length) < 0) {
        return false;
    }

    return true;
}
}
#endif /* __iros__ */

#ifdef __linux__
#include <sys/inotify.h>

namespace App {
FileWatcher::FileWatcher() {
    int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (fd < 0) {
        return;
    }

    set_fd(fd);
    set_selected_events(NotifyWhen::Readable);
    enable_notifications();
}

void FileWatcher::initialize() {
    on<ReadableEvent>([this](auto&) {
        char buffer[1024] __attribute__((aligned(__alignof__(inotify_event))));
        ssize_t ret;
        while ((ret = read(fd(), buffer, sizeof(buffer))) > 0) {
            struct inotify_event* event;
            for (char* ptr = buffer; ptr < buffer + ret; ptr += sizeof(inotify_event) + event->len) {
                event = (struct inotify_event*) ptr;

                auto path_p = m_identifier_to_path.get(event->wd);
                if (!path_p) {
                    continue;
                }
                auto path = *path_p;

                if (event->mask & IN_MODIFY) {
                    emit<PathChangeEvent>(path);
                }

                if (event->mask & IN_DELETE_SELF) {
                    unwatch(path);
                    emit<PathRemovedEvent>(path);
                }
            }
        }
    });
}

FileWatcher::~FileWatcher() {
    if (valid()) {
        close(fd());
    }
}

bool FileWatcher::watch(const String& path) {
    int ret = inotify_add_watch(fd(), path.string(), IN_MODIFY | IN_DELETE_SELF);
    if (ret < 0) {
        return false;
    }

    m_identifier_to_path.put(ret, path);
    m_path_to_indentifier.put(path, ret);
    return true;
}

bool FileWatcher::unwatch(const String& path) {
    auto identifer_p = m_path_to_indentifier.get(path);
    if (!identifer_p) {
        return false;
    }
    auto identifer = *identifer_p;

    m_identifier_to_path.remove(identifer);
    m_path_to_indentifier.remove(path);

    int ret = inotify_rm_watch(fd(), identifer);
    if (ret < 0) {
        return false;
    }

    return true;
}
}
#endif
