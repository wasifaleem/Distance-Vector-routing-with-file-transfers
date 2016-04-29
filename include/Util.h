#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>
#include <netdb.h>

namespace util {
    bool connect_to(int *sock_fd, const char *ip, const char *port, int ai_socktype);
    bool bind_listen_on(int *sock_fd, const char *port);

    const bool valid_inet(const std::string ip);
    const std::string get_ip(const sockaddr_storage addr);
    const bool send_string(int sock_fd, const std::string data);
    const bool send_buff(int sock_fd, const std::vector<char> data);
    const bool send_buff(int sock_fd, const char *buff, unsigned long size);
    const std::string primary_ip();

    const uint16_t ntouint16(const void *buffer);
    const uint8_t touint8(const void *buffer);
}
#endif //UTIL_H
