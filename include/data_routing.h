#ifndef DATA_ROUTING_H
#define DATA_ROUTING_H

#include <stdint.h>
#include <set>
#include <utility>

#define SENDFILE_PAYLOAD_HEADER_SIZE 8

#define DATA_PACKET_HEADER_SIZE 12
#define DATA_PACKET_PAYLOAD_SIZE 1024

namespace data {
    struct __attribute__((__packed__)) SENDFILE_PAYLOAD_HEADER {
        uint32_t dest_ip;
        uint8_t init_ttl;
        uint8_t transfer_id;
        uint16_t init_seq_no;
    };

    struct __attribute__((__packed__)) DATA_PACKET_HEADER {
        uint32_t dest_ip;
        uint8_t transfer_id;
        uint8_t ttl;
        uint16_t seq_no;
        unsigned fin:1;
        unsigned padding:31;
    };

    struct __attribute__((__packed__)) SENDFILE_STATS_HEADER {
        uint8_t transfer_id;
        uint8_t ttl;
        uint16_t padding;
    };

    void sendfile(int sock_fd, char *payload, uint16_t ctrl_payload_len);

    void sendfile_stats(int sock_fd, char *payload);
    void last_data_pkt(int sock_fd);
    void penultimate_data_pkt(int sock_fd);

    int new_data_conn(int sock_index);

    bool is_set(int sock_index);

    bool receive(int data_fd);
}
#endif //DATA_ROUTING_H
