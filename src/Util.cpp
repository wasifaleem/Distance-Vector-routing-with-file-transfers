#include "../include/Util.h"
#include "../include/global.h"
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <sstream>

namespace util {
    const bool valid_inet(const std::string ip) {
        struct sockaddr_in sa;
        return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
    }

    const std::string get_ip(const sockaddr_storage addr) {
        if (addr.ss_family == AF_INET) { // IPv4
            char ip[INET_ADDRSTRLEN];
            return std::string(inet_ntop(addr.ss_family,
                                         &(((struct sockaddr_in *) &addr)->sin_addr),
                                         ip, INET_ADDRSTRLEN));
        } else {                          // IPv6
            char ip[INET6_ADDRSTRLEN];
            return std::string(inet_ntop(addr.ss_family,
                                         &(((struct sockaddr_in6 *) &addr)->sin6_addr),
                                         ip, INET6_ADDRSTRLEN));
        }
    }

    const bool send_string(int sock_fd, const std::string data) {
        return send_buff(sock_fd, data.c_str(), data.length());
    }

    const bool send_buff(int sock_fd, const std::vector<char> data) {
        return send_buff(sock_fd, &data[0], data.size());
    }

    const bool send_buff(int sock_fd, const char *buf, unsigned long size) {
        unsigned long total = 0;
        unsigned long bytes_remaining = size;
        ssize_t n = 0;

        while (total < size) {
            n = send(sock_fd, buf + total, bytes_remaining, 0);
            if (n == -1) { break; }
            total += n;
            bytes_remaining -= n;
        }

        return n != -1;
    }

    bool bind_listen_on(int *sock_fd, const char *port) {
        struct addrinfo *result, *temp;
        struct addrinfo hints;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;;

        int getaddrinfo_result;
        int reuse = 1;
        if ((getaddrinfo_result = getaddrinfo(NULL, port, &hints, &result)) != 0) {
            ERROR("getaddrinfo: " << gai_strerror(getaddrinfo_result));
            return false;
        }

        for (temp = result; temp != NULL; temp = temp->ai_next) {
            *sock_fd = socket(temp->ai_family, temp->ai_socktype, temp->ai_protocol);
            if (*sock_fd == -1)
                continue;

            if (setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
                ERROR("Cannot setsockopt() on: " << *sock_fd << " error: " << strerror(errno));
                close(*sock_fd);
                continue;
            }

            if (bind(*sock_fd, temp->ai_addr, temp->ai_addrlen) == 0) {
                break;
            }

            close(*sock_fd);
        }
        freeaddrinfo(result);

        if (temp == NULL) {
            ERROR("Cannot find a socket to bind to; errno:" << strerror(errno));
            return false;
        }

        if (listen(*sock_fd, BACKLOG) == -1) {
            ERROR("Cannot listen on: " << *sock_fd << " errno: " << strerror(errno));
            return false;
        }
        return true;
    }

    bool connect_to(int *sock_fd, const char *ip, const char *port, int ai_socktype) {
        struct addrinfo *result, *temp;
        struct addrinfo hints;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = ai_socktype;

        int getaddrinfo_result;
        int reuse = 1;
        if ((getaddrinfo_result = getaddrinfo(ip, port, &hints, &result)) != 0) {
            ERROR("getaddrinfo: " << gai_strerror(getaddrinfo_result));
            return false;
        }

        for (temp = result; temp != NULL; temp = temp->ai_next) {
            *sock_fd = socket(temp->ai_family, temp->ai_socktype, temp->ai_protocol);
            if (*sock_fd == -1)
                continue;

            if (setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
                ERROR("Cannot setsockopt() on: " << *sock_fd << "  error: " << strerror(errno));
                close(*sock_fd);
                continue;
            }

            if (connect(*sock_fd, temp->ai_addr, temp->ai_addrlen) == -1) {
                close(*sock_fd);
                ERROR("Cannot connect to " << ip << " errno:" << strerror(errno));
                continue;
            }
            break;
        }
        freeaddrinfo(result);

        if (temp == NULL) {
            ERROR("Cannot find a socket to bind to, errno: " << strerror(errno));
            return false;
        }

        return true;
    }

    const std::string primary_ip() {
        int sock_fd = 0;

        if (connect_to(&sock_fd, "8.8.8.8", "53", SOCK_DGRAM)) {
            struct sockaddr_storage in;
            socklen_t in_len = sizeof(in);

            if (getsockname(sock_fd, (struct sockaddr *) &in, &in_len) == -1) {
                ERROR("Cannot getsockname on: " << sock_fd << ", error: " << strerror(errno));
                return EMPTY_STRING;
            }

            if (sock_fd != 0) {
                close(sock_fd);
            }
            return get_ip(in);
        }
        return EMPTY_STRING;
    }


    const uint16_t ntouint16(const void *buffer) {
        uint16_t i = 0;
        memcpy(&i, buffer, 2);
        return ntohs(i);
    }

    const uint8_t touint8(const void *buffer) {
        uint8_t i = 0;
        memcpy(&i, buffer, 1);
        return (i);
    }
}