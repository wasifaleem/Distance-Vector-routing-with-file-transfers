#include "Router.h"
#include "global.h"
#include <iostream>
#include <Util.h>
#include <unistd.h>
#include <cstdlib>
#include <errno.h>
#include <cstring>
#include <inttypes.h>

void Router::start() {
    LOG("Router started with: " << control_port << " as controller port.");
    do_bind_listen();
    select_loop();
}

void Router::do_bind_listen() {
    if (util::bind_listen_on(&controller_accept_fd, control_port)) {
        LOG("Created controller socket.");
    } else {
        ERROR("Cannot create controller socket for listening.");
        exit(EXIT_FAILURE);
    }
}

void Router::select_loop() {
    FD_ZERO(&all_fd);
    FD_ZERO(&read_fd);

    FD_SET(controller_accept_fd, &all_fd);
    max_fd = controller_accept_fd;

    while (true) {
        read_fd = all_fd;
        if (select(max_fd + 1, &read_fd, NULL, NULL, NULL) == -1) {
            ERROR("SELECT error:" << strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            // Controller accept
            accept_controller();
            // Controller receive
            receive_controller();
        }
    }
}

void Router::accept_controller() {
    if (FD_ISSET(controller_accept_fd, &read_fd)) {
        struct sockaddr_storage controller_addr;
        socklen_t controller_addr_len = sizeof(controller_addr);
        int new_controller_fd;

        if ((new_controller_fd = accept(controller_accept_fd,
                                        (struct sockaddr *) &controller_addr,
                                        &controller_addr_len)) < 0) {
            ERROR("ACCEPT " << "fd:" << new_controller_fd << " error:" << strerror(errno));
        } else {
            controller_ip = util::get_ip(controller_addr);
            LOG("New controller from ip: " << controller_ip);

            if (controller_fd > 0) {
                FD_CLR(controller_fd, &all_fd);
            }
            controller_fd = new_controller_fd;
            FD_SET(controller_fd, &all_fd);
            if (max_fd < controller_fd) { // update max_fd
                max_fd = controller_fd;
            }
        }
    }
}

void Router::receive_controller() {
    char buffer[PKT_WIDTH];
    memset(buffer, 0, PKT_WIDTH);
    ssize_t bytes_read_count;

    if (FD_ISSET(controller_fd, &read_fd)) {
        if ((bytes_read_count = recv(controller_fd, buffer, sizeof buffer, 0)) > 0) {
            for (unsigned i = 0; i < bytes_read_count; ++i) {
                controller_buff.push_back(buffer[i]);
            }
            controller_recv_len += bytes_read_count;

            if (controller_buff.size() == CONTROLLER_HEADER_LEN) {
                controller_payload_len = util::ntouint16(&buffer[2]);
            }

            if (controller_recv_len >= CONTROLLER_HEADER_LEN &&
                controller_recv_len == (unsigned) (controller_payload_len + CONTROLLER_HEADER_LEN)) {
                controller_msg_handle();
                controller_buff.clear();
                controller_recv_len = controller_payload_len = 0;
            }
        } else {
            if (bytes_read_count == 0) {
                LOG("Controller connection closed normally");
                close(controller_fd);
                FD_CLR(controller_fd, &all_fd);
            } else {
                ERROR("Controller receive error: " << strerror(errno));
            }
        }
    }
}

void Router::controller_msg_handle() {
    controller::request_header h = controller::parse_header(&controller_buff[0]);
    controller::message_type t = static_cast<controller::message_type>(h.control_code);
    LOG("Controller pkt:" << t << ": " << controller_buff);
    switch (t) {
        case controller::AUTHOR: {
            std::string payload(AUTHOR_PAYLOAD);
            std::vector<char> p(payload.begin(), payload.end());
            util::send_buff(controller_fd, controller::response(controller::AUTHOR, controller_ip, &p));
            break;
        }
        case controller::INIT: {
            struct controller::routers r = controller::parse_init(&controller_buff[CONTROLLER_HEADER_LEN]);
            util::send_buff(controller_fd, controller::response(controller::INIT, controller_ip, NULL));
            break;
        }
        case controller::ROUTING_TABLE:
            break;
        case controller::UPDATE:
            break;
        case controller::CRASH:
            break;
        case controller::SENDFILE:
            break;
        case controller::SENDFILE_STATS:
            break;
        case controller::LAST_DATA_PACKET:
            break;
        case controller::PENULTIMATE_DATA_PACKET:
            break;
    }
}




