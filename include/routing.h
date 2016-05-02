#ifndef ROUTING_UPDATE_H
#define ROUTING_UPDATE_H

#include <string>
#include <vector>
#include <stdint.h>
#include <map>
#include <sys/time.h>

enum router_type {
    NEIGHBOUR,
    SELF,
    NON_NEIGHBOUR // other routers
};
enum router_status {
    INITIALIZED, ACTIVE, INACTIVE
};

struct router {
    uint16_t router_id;
    uint16_t router_port;
    uint16_t data_port;
    uint16_t cost;
    uint32_t ip;
    char *ip_str;

    router_type type;
    router_status status;
    timeval last_update;
};

struct routers {
    uint16_t router_count;
    uint16_t update_interval;
    std::vector<router> routers;
};

#define ROUTING_UPDATE_HEADER_SIZE 8
#define ROUTING_UPDATE_ENTRY_SIZE 12

struct __attribute__((__packed__)) ROUTING_UPDATE_HEADER {
    uint16_t update_size;
    uint16_t src_port;
    uint32_t src_ip_addr;
};

struct __attribute__((__packed__)) ROUTING_UPDATE_ENTRY {
    uint32_t ip_addr;
    uint16_t port;
    uint16_t padding;
    uint16_t id;
    uint16_t cost;
};

#define ROUTING_TABLE_ENTRY_SIZE 8
struct __attribute__((__packed__)) ROUTING_TABLE_ENTRY {
    uint16_t id;
    uint16_t padding;
    uint16_t next_hop_id;
    uint16_t cost;
};

struct route {
    uint16_t cost;
    uint16_t next_hop_id; // via
};

const router *get_self();

const routers parse_init(char *payload);

void send_routing_updates();


char *routing_payload();

void receive_update(int sock_fd);

void routing_table_response(int controller_fd);

void update_cost(int controller_fd, char *payload);

void process_update(ROUTING_UPDATE_HEADER *header, std::vector<ROUTING_UPDATE_ENTRY> routing_updates);

router *find_by_port_ip(uint16_t port, uint32_t ip);

void recompute_routing_table();

void timeout_handler();

#endif //ROUTING_UPDATE_H
