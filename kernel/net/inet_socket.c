#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <kernel/net/ethernet.h>
#include <kernel/net/inet_socket.h>
#include <kernel/net/interface.h>
#include <kernel/net/ip.h>
#include <kernel/net/port.h>
#include <kernel/net/socket.h>
#include <kernel/net/udp.h>

int net_inet_bind(struct socket *socket, const struct sockaddr_in *addr, socklen_t addrlen) {
    assert(socket);

    if (addr->sin_family != AF_INET || addrlen < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    struct inet_socket_data *data = calloc(1, sizeof(struct inet_socket_data));
    socket->private_data = data;

    if (addr->sin_port == 0) {
        int ret = net_bind_to_ephemeral_port(socket->id, &data->source_port);
        if (ret < 0) {
            return ret;
        }
    } else {
        // TODO: bind to a specified port
        return -EADDRINUSE;
    }

    socket->state = BOUND;
    return 0;
}

int net_inet_close(struct socket *socket) {
    assert(socket);

    struct inet_socket_data *data = socket->private_data;
    if (data) {
        net_unbind_port(data->source_port);
        free(data);
    }

    return 0;
}

int net_inet_socket(int domain, int type, int protocol) {
    assert(domain == AF_INET);

    if (type == SOCK_STREAM || (protocol != IPPROTO_ICMP && protocol != IPPROTO_UDP)) {
        return -EPROTONOSUPPORT;
    }

    int fd;
    struct socket *socket = net_create_socket(domain, type, protocol, &fd);
    (void) socket;

    return fd;
}

ssize_t net_inet_sendto(struct socket *socket, const void *buf, size_t len, int flags, const struct sockaddr_in *dest, socklen_t addrlen) {
    (void) flags;

    assert(socket);
    assert((socket->type & SOCK_TYPE_MASK) == SOCK_RAW || (socket->type & SOCK_TYPE_MASK) == SOCK_DGRAM);

    if (dest->sin_family != AF_INET || addrlen < sizeof(struct sockaddr_in)) {
        return -EINVAL;
    }

    struct network_interface *interface = net_get_interface_for_ip(ip_v4_from_uint(dest->sin_addr.s_addr));
    assert(interface);

    if ((socket->type & SOCK_TYPE_MASK) == SOCK_RAW) {
        return net_send_ip_v4(interface, socket->protocol, ip_v4_from_uint(dest->sin_addr.s_addr), buf, len);
    }

    assert(socket->type == SOCK_DGRAM && socket->protocol == IPPROTO_UDP);
    if (socket->state != BOUND) {
        struct sockaddr_in to_bind = { AF_INET, 0, { 0 }, { 0 } };
        int ret = net_inet_bind(socket, &to_bind, sizeof(struct sockaddr_in));
        if (ret < 0) {
            return ret;
        }
    }

    if (dest == NULL) {
        return -EINVAL;
    }

    struct inet_socket_data *data = socket->private_data;
    struct ip_v4_address dest_ip = ip_v4_from_uint(dest->sin_addr.s_addr);
    return net_send_udp(interface, dest_ip, data->source_port, ntohs(dest->sin_port), len, buf);
}

ssize_t net_inet_recvfrom(struct socket *socket, void *buf, size_t len, int flags, struct sockaddr_in *source, socklen_t *addrlen) {
    (void) flags;
    (void) source;
    (void) addrlen;

    return net_generic_recieve(socket, buf, len);
}