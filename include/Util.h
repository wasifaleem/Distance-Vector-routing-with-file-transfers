#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <vector>
#include <netdb.h>

namespace util {
    bool connect_to(int *sock_fd, const char *ip, const char *port, int ai_socktype);

    bool bind_to(int *sock_fd, const char *port, int ai_socktype);

    bool bind_listen_on(int *sock_fd, const char *port);

    bool udp_socket(int *sock_fd, const char *ip, const char *port, struct addrinfo *ret);

    const bool valid_inet(const std::string ip);

    const std::string get_ip(const sockaddr_storage addr);

    const bool send_string(int sock_fd, const std::string data);

    const bool send_buff(int sock_fd, const std::vector<char> data);

    const bool send_buff(int sock_fd, const char *buff, unsigned long size);

    const std::string primary_ip();

    const uint8_t toui8(const char *buffer, unsigned int &offset);

    const uint16_t ntohui16(const char *buffer, unsigned int &offset);

    const uint32_t toui32(const char *buffer, unsigned int &offset);

    const std::string time_str(time_t t);

    const std::string log_time();

    const std::string to_port_str(uint16_t port);
}
#endif //UTIL_H
