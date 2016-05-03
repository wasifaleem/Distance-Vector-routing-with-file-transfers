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
                int data_fd = 0;
                if (util::connect_to(&data_fd, next_hop->ip_str.c_str(), util::to_port_str(next_hop->data_port).c_str(),
                                     SOCK_STREAM)) {
                    long file_size;
                    std::ifstream file(filename.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
                    if (file.is_open()) {
                        file_size = file.tellg();
                        long num_packets = file_size / DATA_PACKET_PAYLOAD_SIZE;
                        file.seekg(0, std::ios::beg);
                        for (int i = 0; i < num_packets; ++i) {
                            char *data_payload = new char[DATA_PACKET_SIZE];
                            bzero(data_payload, DATA_PACKET_SIZE);
                            stats[copy.transfer_id].insert(
                                    std::pair<uint8_t, uint16_t>(copy.init_ttl, copy.init_seq_no));

                            struct DATA_PACKET *data_packet = (struct DATA_PACKET *) (data_payload);
                            data_packet->dest_ip = copy.dest_ip;
                            data_packet->transfer_id = copy.transfer_id;
                            data_packet->ttl = copy.init_ttl;
                            data_packet->seq_no = htons(copy.init_seq_no++);
                            if (i == (num_packets - 1)) {
                                data_packet->fin = 1;
                            } else {
                                data_packet->fin = 0;
                            }
                            data_packet->padding = 0;
                            file.read(data_packet->payload, DATA_PACKET_PAYLOAD_SIZE);
                            sendALL(data_fd, data_payload, DATA_PACKET_SIZE);
                            update_last_data_packet(data_payload);
                        }
                        file.close();
                        LOG("Sent file " << filename << " in " << num_packets << " chunks.");
                    }
                    close(data_fd);
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
        char *data_packet = new char[DATA_PACKET_SIZE];
        bzero(data_packet, DATA_PACKET_SIZE);

        if (recvALL(data_fd, data_packet, DATA_PACKET_SIZE) < 0) {
            delete[](data_packet);
            close_fd(data_fd);
            return false;
        }

        struct DATA_PACKET *data = (struct DATA_PACKET *) data_packet;
        // decrement TTL
        data->ttl -= 1;
        // update stats
        stats[data->transfer_id].insert(std::pair<uint8_t, uint16_t>(data->ttl, ntohs(data->seq_no)));
        int transfer_id = (int) data->transfer_id;

        const router *self = get_self();
        if (self->ip == data->dest_ip) {
            file_data[data->transfer_id].insert(file_data[data->transfer_id].end(), data->payload,
                                                data->payload + DATA_PACKET_PAYLOAD_SIZE);
            if (data->fin) {
                std::stringstream ss;
                ss << "file-" << transfer_id;
                std::string file_name = ss.str();
                // write file data.
                std::ofstream os(file_name.c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
                std::copy(file_data[data->transfer_id].begin(), file_data[data->transfer_id].end(),
                          std::ostreambuf_iterator<char>(os));
                os.close();
                file_data[data->transfer_id].clear();
                LOG("RECEIVED file: " << file_name);
            } else {
                LOG("RECEIVED seq-no: " << ntohs(data->seq_no) << " of transfer: " << transfer_id);
            }
        } else { // Forward to next hop
            if (data->ttl == 0) {
                LOG("Drop data pkt: end of TTL for id:" << transfer_id << " seq:" << ntohs(data->seq_no));
                delete[](data_packet);
                return true;
            } else {
                route *route = route_for_destination(data->dest_ip);
                if (route != NULL && route->cost != INF) {
                    router *next_hop = find_by_id(route->next_hop_id);
                    if (next_hop != NULL) {
                        int data_fd = 0;
                        if (util::connect_to(&data_fd, next_hop->ip_str.c_str(),
                                             util::to_port_str(next_hop->data_port).c_str(),
                                             SOCK_STREAM)) {
                            sendALL(data_fd, data_packet, DATA_PACKET_SIZE);
                            update_last_data_packet(data_packet);
                            close(data_fd);
                            LOG("FWD data pkt id:" << transfer_id << " seq:" << ntohs(data->seq_no) << " ttl:" <<
                                data->ttl);
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
                    ERROR("Cannot find route/INF for: " << std::string(inet_ntoa(*(struct in_addr *) &data->dest_ip)));
                }
            }
        }

        return true;
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