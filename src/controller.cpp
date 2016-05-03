#include <controller.h>
#include <cstring>
#include <netinet/in.h>
#include <global.h>
#include <unistd.h>
#include <cstdlib>
#include <set>
#include <map>
#include <iostream>
#include <errno.h>
#include <Router.h>
#include <cstdio>

#include "../include/network_util.h"
#include "../include/control_header_lib.h"
#include "../include/author.h"
#include "routing.h"
#include "data_routing.h"

namespace controller {
    static std::set<int> controller_fds;

    void init_response(int fd, char *payload);

    int do_bind_listen(const char *control_port) {
        int controller_accept_fd;
        if (util::bind_listen_on(&controller_accept_fd, control_port)) {
            LOG("Created CONTROLLER socket.");
        } else {
            ERROR("Cannot create CONTROLLER socket for listening.");
            exit(EXIT_FAILURE);
        }
        return controller_accept_fd;
    }

    int accept(int controller_accept_fd) {
        struct sockaddr_storage controller_addr;
        socklen_t controller_addr_len = sizeof(controller_addr);
        int new_controller_fd;

        if ((new_controller_fd = accept(controller_accept_fd,
                                        (struct sockaddr *) &controller_addr,
                                        &controller_addr_len)) < 0) {
            ERROR("ACCEPT CONTROLLER" << "fd:" << new_controller_fd << " error:" << strerror(errno));
        } else {
            LOG("New controller from ip: " << util::get_ip(controller_addr));
            controller_fds.insert(new_controller_fd);
        }
        return new_controller_fd;
    }

    void close_fd(int controller_fd) {
        LOG("CONTROLLER: leaving " << controller_fd);
        controller_fds.erase(controller_fd);
        close(controller_fd);
    }

    bool is_set(int controller_fd) {
        return controller_fds.count(controller_fd) > 0;
    }

    void crash_now(int fd);

    bool receive(int controller_fd) {

        char *cntrl_header, *cntrl_payload;
        uint8_t control_code;
        uint16_t payload_len;

        /* Get control header */
        cntrl_header = (char *) malloc(sizeof(char) * CNTRL_HEADER_SIZE);
        bzero(cntrl_header, CNTRL_HEADER_SIZE);

        if (recvALL(controller_fd, cntrl_header, CNTRL_HEADER_SIZE, 0) < 0) {
            free(cntrl_header);
            close_fd(controller_fd);
            return false;
        }

        struct CONTROL_HEADER *header = (struct CONTROL_HEADER *) cntrl_header;
        control_code = header->control_code;
        payload_len = ntohs(header->payload_len);

        free(cntrl_header);

        /* Get control payload */
        if (payload_len != 0) {
            cntrl_payload = (char *) malloc(sizeof(char) * payload_len);
            bzero(cntrl_payload, payload_len);

            if (recvALL(controller_fd, cntrl_payload, payload_len, 0) < 0) {
                free(cntrl_payload);
                close_fd(controller_fd);
            }
        }

        /* Triage on control_code */
        message_type t = static_cast<message_type>(control_code);
        switch (t) {
            case AUTHOR: {
                author_response(controller_fd);
                break;
            }
            case INIT: {
                init_response(controller_fd, cntrl_payload);
                break;
            }
            case ROUTING_TABLE: {
                routing_table_response(controller_fd);
                break;
            }
            case UPDATE: {
                update_cost(controller_fd, cntrl_payload);
                break;
            }
            case CRASH: {
                crash_now(controller_fd);
                break;
            }
            case SENDFILE: {
                data::sendfile(controller_fd, cntrl_payload, payload_len);
                break;
            }
            case SENDFILE_STATS:
                break;
            case LAST_DATA_PACKET:
                break;
            case PENULTIMATE_DATA_PACKET:
                break;
        }

        if (payload_len != 0) free(cntrl_payload);
        return true;
    }

    void crash_now(int fd) {
        char *cntrl_response = create_response_header(fd, CRASH, 0, 0);
        sendALL(fd, cntrl_response, CNTRL_RESP_HEADER_SIZE);
        free(cntrl_response);
        LOG("Goodbye!");
        exit(EXIT_SUCCESS);
    }

    void init_response(int fd, char *payload) {
        char *cntrl_response = create_response_header(fd, INIT, 0, 0);
        sendALL(fd, cntrl_response, CNTRL_RESP_HEADER_SIZE);
        parse_init(payload);
        free(cntrl_response);

        int router_fd, data_fd;
        const router *self = get_self();
        if (util::bind_to(&router_fd, util::to_port_str(self->router_port).c_str(), SOCK_DGRAM)) {
            LOG("Created ROUTER socket " << self->router_port);
            Router::set_router_socket(router_fd);
            send_routing_updates();
        } else {
            ERROR("Cannot create ROUTER socket: " << self->router_port);
        }
        if (util::bind_listen_on(&data_fd, util::to_port_str(self->data_port).c_str())) {
            LOG("Created DATA socket " << self->data_port);
            Router::set_data_socket(data_fd);
        } else {
            ERROR("Cannot create DATA socket for listening: " << self->data_port);
        }
    }
}