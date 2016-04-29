#include <controller.h>
#include <cstring>
#include <netinet/in.h>
#include <global.h>
#include <arpa/inet.h>

namespace controller {
    uint8_t toui8(const char *buffer, unsigned int &offset);
    uint16_t ntohui16(const char *buffer, unsigned int &offset);
    uint32_t toui32(const char *buffer, unsigned int &offset);

    const request_header parse_header(char *buffer) {
        unsigned offset = 0;
        struct request_header header;
        header.ip = toui32(buffer, offset);
        header.control_code = toui8(buffer, offset);
        header.response_time = toui8(buffer, offset);
        header.payload_length = ntohui16(buffer, offset);
        return header;
    }

    const routers parse_init(char *buffer) {
        struct routers rs;
        unsigned offset = 0;
        rs.router_count = ntohui16(buffer, offset);
        rs.update_interval = ntohui16(buffer, offset);
        for (int i = 0; i < rs.router_count; ++i) {
            struct router r;
            r.router_id = ntohui16(buffer, offset);
            r.router_port = ntohui16(buffer, offset);
            r.data_port = ntohui16(buffer, offset);
            r.cost = ntohui16(buffer, offset);
            r.ip = toui32(buffer, offset);
            r.ip_str = std::string(inet_ntoa(*(struct in_addr *) &r.ip));
            rs.routers.push_back(r);
        }
        return rs;
    }

    uint8_t toui8(const char *buffer, unsigned int &offset) {
        uint8_t ui8 = 0;
        memcpy(&ui8, buffer + offset, 1);
        offset += 1;
        return ui8;
    }

    uint16_t ntohui16(const char *buffer, unsigned int &offset) {
        uint16_t ui16 = 0;
        memcpy(&ui16, buffer + offset, 2);
        offset += 2;
        return ntohs(ui16);
    }

    uint32_t toui32(const char *buffer, unsigned int &offset) {
        uint32_t ui32 = 0;
        memcpy(&ui32, buffer + offset, 4);
        offset += 4;
        return ui32;
    }

    const std::vector<char> response(message_type type, std::string ip, std::vector<char> *payload) {
        uint8_t payload_size = 0;
        if (payload != NULL) {
            payload_size += payload->size();
        }
        std::vector<char> resp(sizeof(struct request_header) + payload_size);
        uint32_t ui32;
        uint8_t ui8 = 0;

        inet_aton(ip.c_str(), (in_addr *) &ui32);
        memcpy(&resp[0], &ui32, 4);
        memcpy(&resp[4], &type, 1);
        memcpy(&resp[5], &ui8, 1);
        uint16_t ps = htons(payload_size);
        memcpy(&resp[6], &ps, 2);
        if (payload != NULL) {
            memcpy(&resp[8], &(*payload)[0], payload_size);
        }
        return resp;
    }
}