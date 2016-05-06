#include "Router.h"
#include "global.h"
#include <cstdlib>
#include <errno.h>
#include <cstring>
#include <routing.h>
#include <data_routing.h>

static int control_socket, router_socket, data_socket, max_fd;
static fd_set read_fd, write_fd, all_fd;

void Router::start() {
    LOG("Router " << util::primary_ip() << " started with: " << control_port << " as controller port.");

    FD_ZERO(&all_fd);
    FD_ZERO(&read_fd);
    FD_ZERO(&write_fd);

    control_socket = controller::do_bind_listen(control_port);

    FD_SET(control_socket, &all_fd);
    max_fd = control_socket;

    select_loop();
}

void Router::select_loop() {
    int selret, sock_index, fd_accept;
    struct timeval start = (struct timeval) {0, CLOCK_TICK};
    while (true) {
        read_fd = all_fd;
//        data::get_write_set(write_fd);
        selret = select(max_fd + 1, &read_fd, &write_fd, NULL, &start);

        if (selret < 0) {
            ERROR("SELECT error:" << strerror(errno));
            // exit(EXIT_FAILURE);
        } else if (selret == 0) {
            timeout_handler();
            start = (struct timeval) {0, CLOCK_TICK};
        } else if (selret > 0) {
            for (sock_index = 0; sock_index <= max_fd; sock_index += 1) {
                if (FD_ISSET(sock_index, &read_fd)) {
                    /* control_socket */
                    if (sock_index == control_socket) {
                        fd_accept = controller::accept(sock_index);

                        /* Add to watched socket list */
                        FD_SET(fd_accept, &all_fd);
                        if (fd_accept > max_fd) max_fd = fd_accept;
                    }
                    else if (sock_index == router_socket) {
                        receive_update(router_socket);
                    }
                    else if (sock_index == data_socket) { /* data_socket */
                        fd_accept = data::new_data_conn(sock_index);

                        /* Add to watched socket list */
                        FD_SET(fd_accept, &all_fd);
                        if (fd_accept > max_fd) max_fd = fd_accept;
                    }
                    else {
                        /* Existing connection */
                        if (controller::is_set(sock_index)) {
                            if (!controller::receive(sock_index)) {
                                FD_CLR(sock_index, &all_fd);
                            }
                        }
                        else if (data::is_set(sock_index)) {
                            if (!data::receive(sock_index)) {
                                FD_CLR(sock_index, &all_fd);
                            }
                        }
                        else {
                            ERROR("Unknown socket index");
                        }
                    }
                } else if (FD_ISSET(sock_index, &write_fd)) {
                    data::write_handler(sock_index);
                }
            }
        }
    }
}

void Router::set_router_socket(int router_fd) {
    router_socket = router_fd;
    FD_SET(router_socket, &all_fd);
    if (router_socket > max_fd) max_fd = router_socket;
}

void Router::set_data_socket(int data_fd) {
    data_socket = data_fd;
    FD_SET(data_socket, &all_fd);
    if (data_socket > max_fd) max_fd = data_socket;
}

void Router::enable_write(int data_fd) {
    if (!FD_ISSET(data_fd, &write_fd)) {
        LOG("enabled: " << data_fd);
        FD_SET(data_fd, &write_fd);
        if (data_fd > max_fd) max_fd = data_fd;
    }
}

void Router::disable_write(int data_fd) {
    if (FD_ISSET(data_fd, &write_fd)) {
        FD_CLR(data_fd, &write_fd);
        LOG("cleared: " << data_fd);
    }
}