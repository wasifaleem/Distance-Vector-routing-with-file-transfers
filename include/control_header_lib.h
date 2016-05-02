#ifndef CONTROL_HANDLER_LIB_H_
#define CONTROL_HANDLER_LIB_H_

#define CNTRL_HEADER_SIZE 8
#define CNTRL_RESP_HEADER_SIZE 8

struct __attribute__((__packed__)) CONTROL_HEADER {
    uint32_t dest_ip_addr;
    uint8_t control_code;
    uint8_t response_time;
    uint16_t payload_len;
};

struct __attribute__((__packed__)) CONTROL_RESPONSE_HEADER {
    uint32_t controller_ip_addr;
    uint8_t control_code;
    uint8_t response_code;
    uint16_t payload_len;
};

char *create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len);



#endif