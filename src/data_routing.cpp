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
#include <queue>
#include <cstdlib>

namespace data {
    static std::set<int> data_fds;
    static std::map<uint8_t, std::set<std::pair<uint8_t, uint16_t> > > stats; // <transfer_id, {(ttl, seq_no)}>
    static char *last = NULL, *penultimate = NULL;
    static std::map<uint8_t, std::vector<char> > file_data; // <transfer_id, [bytes]>

    struct file_chunk {
        struct DATA_PACKET_HEADER header;
        char *payload;
        bool is_origin;
        int controller_fd;
        ssize_t sent;
    };

    static std::map<int, std::queue<struct file_chunk> > send_buffer;

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

                            send_buffer[data_fd].push(
                                    (struct file_chunk) {*data_header, data_pkt, true, controller_fd, 0});

//                            sendALL(data_fd, data_pkt, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
//                            update_last_data_packet(data_pkt);
                        }
                        file.close();
                        LOG("Sent file " << filename << " in " << num_packets << " chunks.");
                    }
//                    close(data_fd);
                } else {
                    ERROR("Cannot connect to data port" << unsigned(next_hop->data_port) << " of router by id: " <<
                                  unsigned(route->next_hop_id));
                }
            } else {
                // Should not happen, assume dest exists
                ERROR("Cannot find router by id: " << unsigned(route->next_hop_id));
            }
        } else {
            // Should not happen, assume dest exists
            ERROR("Cannot find route/INF for: " << std::string(inet_ntoa(*(struct in_addr *) &copy.dest_ip)));
        }
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
        char *data_header_buff = new char[DATA_PACKET_HEADER_SIZE];
        bzero(data_header_buff, DATA_PACKET_HEADER_SIZE);

        if (recvALL(data_fd, data_header_buff, DATA_PACKET_HEADER_SIZE) < 0) {
            delete[](data_header_buff);
            close_fd(data_fd);
            return false;
        }

        char *data_payload_buff = new char[DATA_PACKET_PAYLOAD_SIZE];
        bzero(data_payload_buff, DATA_PACKET_PAYLOAD_SIZE);

        if (recvALL(data_fd, data_payload_buff, DATA_PACKET_PAYLOAD_SIZE) < 0) {
            delete[](data_payload_buff);
            close_fd(data_fd);
            return false;
        }

        struct DATA_PACKET_HEADER *data_header = (struct DATA_PACKET_HEADER *) data_header_buff;
        // decrement TTL
        data_header->ttl -= 1;
        // update stats
        stats[data_header->transfer_id].insert(
                std::pair<uint8_t, uint16_t>(data_header->ttl, ntohs(data_header->seq_no)));
        unsigned transfer_id = data_header->transfer_id;

        if (data_header->ttl == 0) {
            LOG("Drop data pkt: end of TTL for id:" << transfer_id << " seq:" << unsigned(ntohs(data_header->seq_no)));
            delete[](data_header_buff);
            return true;
        } else {
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
                    LOG("RECEIVED seq-no: " << unsigned(ntohs(data_header->seq_no)) << " of transfer: " << transfer_id);
                }
            } else { // Forward to next hop
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

                            send_buffer[data_fd].push((struct file_chunk) {*data_header, data_pkt, false, 0, 0});

//                            sendALL(next_hop_fd, data_pkt, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
//                            update_last_data_packet(data_pkt);
//                            close(next_hop_fd);
                            LOG("FWD data pkt id:" << transfer_id << " seq:" << unsigned(ntohs(data_header->seq_no)) <<
                                " ttl:" << unsigned(data_header->ttl));
                        } else {
                            ERROR("Cannot connect to data port" << unsigned(next_hop->data_port) <<
                                  " of router by id: " <<
                                  unsigned(route->next_hop_id));
                        }
                    } else {
                        // Should not happen, assume dest exists
                        ERROR("Cannot find router by id: " << unsigned(route->next_hop_id));
                    }
                } else {
                    // Should not happen, assume dest exists
                    ERROR("Cannot find route/INF for: " <<
                          std::string(inet_ntoa(*(struct in_addr *) &data_header->dest_ip)));
                }
            }
        }

        return true;
    }

    void sendfile_stats(int sock_fd, char *payload) {
        unsigned offset = 0;
        uint8_t transfer_id = util::toui8(payload, offset);
        unsigned long count = stats[transfer_id].size();
        if (count != 0) {
            uint16_t payload_len = 4 + (count * 2);
            char *cntrl_payload = new char[payload_len];
            bzero(cntrl_payload, payload_len);
            struct SENDFILE_STATS_HEADER *header = (struct SENDFILE_STATS_HEADER *) cntrl_payload;
            header->transfer_id = transfer_id;
            header->padding = htons(0);
            offset = 4;

            for (std::set<std::pair<uint8_t, uint16_t> >::iterator it = stats[transfer_id].begin();
                 it != stats[transfer_id].end(); ++it) {
                header->ttl = (*it).first;
                uint16_t seq_no = htons(it->second);
                memcpy(cntrl_payload + offset, &seq_no, sizeof(uint16_t));
                offset += 2;
            }
            char *cntrl_header = create_response_header(sock_fd, controller::SENDFILE_STATS, 0, payload_len);
            char *cntrl_response = new char[CNTRL_RESP_HEADER_SIZE + payload_len];
            bzero(cntrl_response, CNTRL_RESP_HEADER_SIZE + payload_len);

            /* Copy Header */
            memcpy(cntrl_response, cntrl_header, CNTRL_RESP_HEADER_SIZE);
            delete[](cntrl_header);
            /* Copy Payload */
            memcpy(cntrl_response + CNTRL_RESP_HEADER_SIZE, cntrl_payload, payload_len);
            sendALL(sock_fd, cntrl_response, CNTRL_RESP_HEADER_SIZE + payload_len);
            delete[](cntrl_response);
        }
    }

    void last_data_pkt(int sock_fd) {
        if (last != NULL) {
            char *cntrl_header = create_response_header(sock_fd, controller::LAST_DATA_PACKET, 0,
                                                        DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
            size_t response_len = CNTRL_RESP_HEADER_SIZE + DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE;
            char *cntrl_response = new char[response_len];
            bzero(cntrl_response, response_len);

            /* Copy Header */
            memcpy(cntrl_response, cntrl_header, CNTRL_RESP_HEADER_SIZE);
            delete[](cntrl_header);
            /* Copy Payload */
            memcpy(cntrl_response + CNTRL_RESP_HEADER_SIZE, last, DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
            sendALL(sock_fd, cntrl_response, response_len);
            delete[](cntrl_response);
        }
    }

    void penultimate_data_pkt(int sock_fd) {
        if (penultimate != NULL) {
            char *cntrl_header = create_response_header(sock_fd, controller::PENULTIMATE_DATA_PACKET, 0,
                                                        DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
            size_t response_len = CNTRL_RESP_HEADER_SIZE + DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE;
            char *cntrl_response = new char[response_len];
            bzero(cntrl_response, response_len);

            /* Copy Header */
            memcpy(cntrl_response, cntrl_header, CNTRL_RESP_HEADER_SIZE);
            delete[](cntrl_header);
            /* Copy Payload */
            memcpy(cntrl_response + CNTRL_RESP_HEADER_SIZE, penultimate,
                   DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE);
            sendALL(sock_fd, cntrl_response, response_len);
            delete[](cntrl_response);
        }
    }

    void get_write_set(fd_set &writefds) {
        FD_ZERO(&writefds);
        for (std::map<int, std::queue<struct file_chunk> >::iterator it = send_buffer.begin();
             it != send_buffer.end(); ++it) {
            if (!(it->second).empty()) {
                FD_SET(it->first, &writefds);
            }
        }
    }

    void write_handler(int sock_fd) {
        while (!send_buffer[sock_fd].empty()) {
            struct file_chunk chunk = send_buffer[sock_fd].front();
//            LOG("Write: " << (int) chunk.header.transfer_id);

            ssize_t nbytes = DATA_PACKET_HEADER_SIZE + DATA_PACKET_PAYLOAD_SIZE, n = 0;

//            while (chunk.sent < nbytes) {
////                n = send(sock_fd, chunk.payload + chunk.sent, (size_t) (nbytes - chunk.sent), MSG_DONTWAIT);
//                n = send(sock_fd, chunk.payload + chunk.sent, (size_t) (nbytes - chunk.sent), 0);
//                if (n == -1) {
//                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                        break;
//                    }
//                }
//                chunk.sent += n;
//                LOG("Wrote: " << chunk.sent);
//            }
//            sendALL(sock_fd, chunk.payload, nbytes);

            chunk.sent = send(sock_fd, chunk.payload + chunk.sent, nbytes, MSG_DONTWAIT);
            int flags = MSG_DONTWAIT;
            while (chunk.sent < nbytes) {
                n = send(sock_fd, chunk.payload + chunk.sent, nbytes - chunk.sent, flags);
                if (n <= 0) {
                    ERROR("sendALL: " << strerror(errno));
                    flags = 0;
                    return;
                } else if (n > 0) {
                    chunk.sent += n;
                    flags = MSG_DONTWAIT;
                }
            }

            if (chunk.is_origin && chunk.header.fin) {
                // send controller response
                char *cntrl_response_header = create_response_header(chunk.controller_fd, controller::SENDFILE, 0, 0);
                sendALL(chunk.controller_fd, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
                delete[](cntrl_response_header);
                LOG("Sent file " << unsigned(chunk.header.transfer_id));
            }
            send_buffer[sock_fd].pop();
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