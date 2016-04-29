#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <string>
#include <vector>

namespace controller {
    enum message_type {
        AUTHOR, INIT, ROUTING_TABLE, UPDATE, CRASH, SENDFILE, SENDFILE_STATS, LAST_DATA_PACKET, PENULTIMATE_DATA_PACKET
    };

    struct request_header {
        uint32_t ip;
        uint8_t control_code;
        uint8_t response_time;
        uint16_t payload_length;
    };

    struct router {
        uint16_t router_id;
        uint16_t router_port;
        uint16_t data_port;
        uint16_t cost;
        uint32_t ip;
        std::string ip_str;
    };

    struct routers {
        uint16_t router_count;
        uint16_t update_interval;
        std::vector<router> routers;
    };

//    struct response_header {
//        uint32_t ip;
//        uint8_t control_code;
//        uint8_t response_code;
//        uint16_t payload_length;
//    };

    const request_header parse_header(char *buffer);

    const routers parse_init(char *buffer);

    const std::vector<char> response(message_type type, std::string ip, std::vector<char> *payload);
}
#endif //CONTROLLER_H
