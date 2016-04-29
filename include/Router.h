#ifndef ROUTER_H
#define ROUTER_H

#include <global.h>
#include <stdlib.h>
#include <netdb.h>
#include <vector>
#include <iostream>
#include "controller.h"


class Router {
public:
    Router(const char *control_port) : control_port(control_port) {
        controller_recv_len = 0;
        controller_payload_len = 0;
    }

    void start();

private:
    const char *control_port;
    int router_fd, controller_fd, controller_accept_fd, max_fd;
    fd_set read_fd, all_fd;

    std::vector<char> controller_buff;
    unsigned controller_recv_len;
    uint16_t controller_payload_len;
    std::string controller_ip;

    void do_bind_listen();

    void select_loop();

    void accept_controller();

    void receive_controller();

    void controller_msg_handle();
};

template<typename T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &v) {
    out << "[";
    size_t last = v.size() - 1;
    for (size_t i = 0; i < v.size(); ++i) {
        out << v[i];
        if (i != last)
            out << ", ";
    }
    out << "]";
    return out;
}

#endif //ROUTER_H
