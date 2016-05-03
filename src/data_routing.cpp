#include <data_routing.h>
#include <routing.h>
#include <strings.h>
#include <string>
#include <arpa/inet.h>
#include <ios>
#include <fstream>
#include <global.h>
#include <iostream>
#include <cmath>
#include <network_util.h>
#include <unistd.h>
#include <stdlib.h>
#include <control_header_lib.h>
#include <controller.h>
#include <cstring>
#include <errno.h>
#include <sstream>

namespace data {
    static std::set<int> data_fds;
    static std::map<uint8_t, std::set<std::pair<uint8_t, uint16_t> > > stats; // <transfer_id, {(ttl, seq_no)}>
    static char *last = NULL, *penultimate = NULL;
    static std::map<uint8_t, std::vector<char> > file_data; // <transfer_id, [bytes]>

    void update_last_data_packet(char *payload);

    void sendfile(int controller_fd, char *payload, uint16_t ctrl_payload_len) {

        struct SENDFILE_PAYLOAD_HEADER *header = (struct SENDFILE_PAYLOAD_HEADER *) payload;
        struct SENDFILE_PAYLOAD_HEADER copy = *header;
        copy.init_seq_no = ntohs(copy.init_seq_no);

        std::string filename = std::string((payload + SENDFILE_PAYLOAD_HEADER_SIZE),
                                           (unsigned long) (ctrl_payload_len - SENDFILE_PAYLOAD_HEADER_SIZE));
        route *route = route_for_destination(copy.dest_ip);
        if (route != NULL && route->cost != INF) {
            router *next_hop = find_by_id(route->next_hop_id);
            if (next_hop != NULL) {
                int data_fd = next_hop->data_socket_fd;
                if (data_fd != 0 ||
                    util::connect_to(&data_fd, next_hop->ip_str.c_str(), util::to_port_str(next_hop->data_port).c_str(),
                                     SOCK_STREAM)) {
                    next_hop->data_socket_fd = data_fd;
                    long file_size;
                    std::ifstream file(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
                    if (file.is_open()) {
                        file_size = file.tellg();
                        long num_packets = file_size / DATA_PACKET_PAYLOAD_SIZE;
                        file.seekg(0, std::ios::beg);
                        for (int i = 0; i < num_packets; ++i) {
                            stats[copy.transfer_id].insert(
                                    std::pair<uint8_t, uint16_t>(copy.init_ttl, copy.init_seq_no));
                            char *data_header_buff = new char[DATA_PACKET_HEADER_SIZE];
                            bzero(data_header_buff, DATA_PACKET_HEADER_SIZE);
                            struct DATA_PACKET_HEADER *data_header = (struct DATA_PACKET_HEADER *) (data_header_buff);
                            data_header->dest_ip = copy.dest_ip;
                            data_header->transfer_id = copy.transfer_id;
                            data_header->ttl = copy.init_ttl;
                            data_header->seq_no = htons(copy.init_seq_no++);
                            if (i == (num_packets - 1)) {
                                data_header->fin = 1;
                            } else {
                                data_header->fin = 0;
                            }
                            data_header->padding = 0;

                            char *data_payload_buff = new char[DATA_PACKET_PAYLOAD_SIZE];
                            bzero(data_payload_buff, DATA_PACKET_PAYLOAD_SIZE);
                            file.read(data_payload_buff, DATA_PACKET_PAYLOAD_SIZE);

                            char *data_pkt = new char[DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE];
                            /* Copy Header */
                            memcpy(data_pkt, data_header_buff, DATA_PACKET_HEADER_SIZE);
                            delete[](data_header_buff);
                            /* Copy Payload */
                            memcpy(data_pkt + DATA_PACKET_HEADER_SIZE, data_payload_buff, DATA_PACKET_PAYLOAD_SIZE);
                            delete[](data_payload_buff);

                            ssize_t bytes = 0, n = 0;

                            bytes = send(data_fd, data_pkt, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE, MSG_DONTWAIT);

                            int flags = MSG_DONTWAIT;
                            while (bytes < DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE) {
                                n += send(data_fd, data_pkt + bytes,
                                          (DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE) - bytes, flags);
                                if (n <= 0) {
                                    ERROR("sendALL: " << strerror(errno));
                                    flags =0;
                                    while (receive(data_fd)) { };
                                } else if (n > 0) {
                                    bytes += n;
                                    flags = MSG_DONTWAIT;
                                }
                            }

//                            sendALL(data_fd, data_pkt, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
                            update_last_data_packet(data_pkt);
                        }
                        file.close();
                        LOG("Sent file " << filename << " in " << num_packets << " chunks.");
                    }
//                    close(data_fd);
                } else {
                    ERROR("Cannot connect to data port" << next_hop->data_port << " of router by id: " <<
                          route->next_hop_id);
                }
            } else {
                // Should not happen, assume dest exists
                ERROR("Cannot find router by id: " << route->next_hop_id);
            }
        } else {
            // Should not happen, assume dest exists
            ERROR("Cannot find route/INF for: " << std::string(inet_ntoa(*(struct in_addr *) &copy.dest_ip)));
        }

        // send controller response
        char *cntrl_response_header = create_response_header(controller_fd, controller::SENDFILE, 0, 0);
        sendALL(controller_fd, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
        delete[](cntrl_response_header);
    }

    int new_data_conn(int sock_index) {
        struct sockaddr_storage data_addr;
        socklen_t data_addr_len = sizeof(data_addr);
        int new_data_fd;

        if ((new_data_fd = accept(sock_index,
                                  (struct sockaddr *) &data_addr,
                                  &data_addr_len)) < 0) {
            ERROR("ACCEPT DATA CONNECTION " << "fd:" << new_data_fd << " error:" << strerror(errno));
        } else {
            LOG("New data connection from ip: " << util::get_ip(data_addr));
            data_fds.insert(new_data_fd);
        }
        return new_data_fd;
    }

    bool is_set(int sock_fd) {
        return data_fds.count(sock_fd) > 0;
    }

    void close_fd(int data_fd) {
        LOG("DATA: leaving " << data_fd);
        data_fds.erase(data_fd);
        close(data_fd);
    }

    bool receive(int data_fd) {
        bool received = false;
        while (true) {

            char *data_full_buff = new char[DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE];

            if (recvALL(data_fd, data_full_buff, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE,
                        MSG_DONTWAIT | MSG_PEEK) != (DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE)) {
                delete[](data_full_buff);
                return false;
            }
            delete[](data_full_buff);

            char *data_header_buff = new char[DATA_PACKET_HEADER_SIZE];
            bzero(data_header_buff, DATA_PACKET_HEADER_SIZE);

            if (recvALL(data_fd, data_header_buff, DATA_PACKET_HEADER_SIZE, 0) < 0) {
                delete[](data_header_buff);
                return received;
            } else {
                received = true;
            }

            char *data_payload_buff = new char[DATA_PACKET_PAYLOAD_SIZE];
            bzero(data_payload_buff, DATA_PACKET_PAYLOAD_SIZE);

            if (recvALL(data_fd, data_payload_buff, DATA_PACKET_PAYLOAD_SIZE, 0) < 0) {
                delete[](data_payload_buff);
                return false;
            }

            struct DATA_PACKET_HEADER *data_header = (struct DATA_PACKET_HEADER *) data_header_buff;
            // decrement TTL
            data_header->ttl -= 1;
            // update stats
            stats[data_header->transfer_id].insert(
                    std::pair<uint8_t, uint16_t>(data_header->ttl, ntohs(data_header->seq_no)));
            int transfer_id = (int) data_header->transfer_id;

            const router *self = get_self();
            if (self->ip == data_header->dest_ip) {
                file_data[data_header->transfer_id].insert(file_data[data_header->transfer_id].end(), data_payload_buff,
                                                           data_payload_buff + DATA_PACKET_PAYLOAD_SIZE);
                if (data_header->fin) {
                    std::stringstream ss;
                    ss << "file-" << transfer_id;
                    std::string file_name = ss.str();
                    // write file data.
                    std::ofstream os(file_name.c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
                    std::copy(file_data[data_header->transfer_id].begin(), file_data[data_header->transfer_id].end(),
                              std::ostreambuf_iterator<char>(os));
                    os.close();
                    file_data[data_header->transfer_id].clear();
                    LOG("RECEIVED file: " << file_name);
                } else {
                    LOG("RECEIVED seq-no: " << ((int) ntohs(data_header->seq_no)) << " of transfer: " << transfer_id);
                }
            } else { // Forward to next hop
                if (data_header->ttl == 0) {
                    LOG("Drop data pkt: end of TTL for id:" << transfer_id << " seq:" << ntohs(data_header->seq_no));
                    delete[](data_header_buff);
                    return true;
                } else {
                    route *route = route_for_destination(data_header->dest_ip);
                    if (route != NULL && route->cost != INF) {
                        router *next_hop = find_by_id(route->next_hop_id);
                        if (next_hop != NULL) {
                            int next_hop_fd = next_hop->data_socket_fd;
                            if (next_hop_fd != 0 || util::connect_to(&next_hop_fd, next_hop->ip_str.c_str(),
                                                                     util::to_port_str(next_hop->data_port).c_str(),
                                                                     SOCK_STREAM)) {
                                next_hop->data_socket_fd = next_hop_fd;
                                char *data_pkt = new char[DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE];
                                /* Copy Header */
                                memcpy(data_pkt, data_header_buff, DATA_PACKET_HEADER_SIZE);
                                delete[](data_header_buff);
                                /* Copy Payload */
                                memcpy(data_pkt + DATA_PACKET_HEADER_SIZE, data_payload_buff, DATA_PACKET_PAYLOAD_SIZE);
                                delete[](data_payload_buff);

                                sendALL(next_hop_fd, data_pkt, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
                                update_last_data_packet(data_pkt);
                                LOG("FWD data pkt id:" << transfer_id << " seq:" <<
                                    ((int) ntohs(data_header->seq_no)) << " ttl:" <<
                                    ((int) data_header->ttl));
                            } else {
                                ERROR("Cannot connect to data port" << next_hop->data_port << " of router by id: " <<
                                      route->next_hop_id);
                            }
                        } else {
                            // Should not happen, assume dest exists
                            ERROR("Cannot find router by id: " << route->next_hop_id);
                        }
                    } else {
                        // Should not happen, assume dest exists
                        ERROR("Cannot find route/INF for: " <<
                              std::string(inet_ntoa(*(struct in_addr *) &data_header->dest_ip)));
                    }
                }
            }
        }
    }


    void update_last_data_packet(char *payload) {
        if (penultimate != NULL && last != NULL) {
            delete[](penultimate);
            penultimate = last;
            last = payload;
        } else if (last != NULL) {
            penultimate = last;
            last = payload;
        } else {
            last = payload;
        }
    }
}