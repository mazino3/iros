set(SOURCES
    event_loop.cpp
    file_watcher.cpp
    input_tracker.cpp
    key_bindings.cpp
    object_bound_coroutine.cpp
    object.cpp
    selectable.cpp
    selectable_file.cpp
    timer.cpp
    udp_socket.cpp
    unix_socket.cpp
    unix_socket_server.cpp
)

add_os_library(libeventloop eventloop TRUE)

target_link_libraries(libeventloop PUBLIC libliim ${PTHREAD_LIB} ${REALTIME_LIB} kernel_headers)
