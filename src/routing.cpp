#include <routing.h>
#include <Util.h>
#include <arpa/inet.h>
#include <global.h>
#include <math.h>
#include <network_util.h>
#include <iostream>
#include <strings.h>
#include <cstring>
#include <errno.h>
#include <sys/time.h>
#include <control_header_lib.h>
#include <controller.h>
#include <stdlib.h>
#include <unistd.h>
#include <Router.h>

typedef uint16_t router_id, cost;
typedef std::map<router_id, cost> distance_vector; // dv:{ router_id: cost}

static router *self;
static struct routers rs;
static long update_counter = 0;

void mark_inactive();

// Routing table
static std::map<router_id, route> routing_table; // <router_id, { cost, next_hop }>

// Distance vectors
static std::map<router_id, distance_vector> distance_vectors; // <router_id, dv:{ router_id: cost}>

void send_routing_updates() {
    if (rs.router_count > 0) {
        ssize_t payload_len = ROUTING_UPDATE_HEADER_SIZE + (rs.router_count * ROUTING_UPDATE_ENTRY_SIZE);
        char *payload = routing_payload();
        for (std::vector<router>::iterator it = rs.routers.begin();
             it != rs.routers.end(); ++it) {
            struct router r = *it;
            if (r.type == NEIGHBOUR && (r.status == ACTIVE || r.status == INITIALIZED)) {
                int sock_fd;
                struct addrinfo addr;
                if (util::udp_socket(&sock_fd, r.ip_str.c_str(), util::to_port_str(r.router_port).c_str(), &addr)
                    && sendToALL(sock_fd, payload, payload_len, &addr) > 0) {
                    LOG("Sent update to router: " << r.router_id << " " << r.ip_str << ":" << r.router_port);
                    close(sock_fd);
                } else {
                    ERROR("Cannot send routing update to: " << r.ip_str << " err:" << strerror(errno));
                }
            }
        }
        delete[] (payload);
    }
}

void receive_update(int sock_fd) {
    if (rs.router_count > 0) {
        char *routing_header, *routing_payload;
        routing_header = new char[ROUTING_UPDATE_HEADER_SIZE];
        bzero(routing_header, ROUTING_UPDATE_HEADER_SIZE);
        struct sockaddr_in from;
        if (recvFromALL(sock_fd, routing_header, ROUTING_UPDATE_HEADER_SIZE, MSG_PEEK, &from) < 0) { // peek!
            delete[] (routing_header);
            return;
        }
        struct ROUTING_UPDATE_HEADER *header = (struct ROUTING_UPDATE_HEADER *) routing_header;
        header->update_size = ntohs(header->update_size);
        header->src_port = ntohs(header->src_port);


        if (header->update_size != 0) {
            int payload_len = ROUTING_UPDATE_HEADER_SIZE + (ROUTING_UPDATE_ENTRY_SIZE * header->update_size);
            routing_payload = new char[payload_len]; // now retrieve in full!
            bzero(routing_payload, payload_len);

            if (recvFromALL(sock_fd, routing_payload, payload_len, 0, &from) < 0) {
                delete[] (routing_payload);
                return;
            }
        } else {
            return;
        }

        std::vector<ROUTING_UPDATE_ENTRY> updates;
        for (int i = 0; i < header->update_size; ++i) {
            struct ROUTING_UPDATE_ENTRY *entry = (struct ROUTING_UPDATE_ENTRY *) (routing_payload +
                                                                                  ROUTING_UPDATE_HEADER_SIZE +
                                                                                  (i * ROUTING_UPDATE_ENTRY_SIZE));
            struct ROUTING_UPDATE_ENTRY copy = *entry;
            copy.port = ntohs(copy.port);
            copy.id = ntohs(copy.id);
            copy.cost = ntohs(copy.cost);

            updates.push_back(copy);
        }
        process_update(header, updates);

        delete[] (routing_header);
        delete[] (routing_payload);
    }
}

void process_update(ROUTING_UPDATE_HEADER *header, std::vector<ROUTING_UPDATE_ENTRY> routing_updates) {
    router *src = find_by_port_ip(header->src_port, header->src_ip_addr);
    if (src != NULL) {
        if (src->status != INACTIVE) {
            if (src->status == INITIALIZED) {
                src->status = ACTIVE;
            }
            timeval t;
            gettimeofday(&t, NULL);
            src->last_update = t;
            distance_vectors[src->router_id].clear();
            for (std::vector<ROUTING_UPDATE_ENTRY>::size_type i = 0; i != routing_updates.size(); i++) {
                ROUTING_UPDATE_ENTRY update = routing_updates[i];
                distance_vectors[src->router_id][update.id] = update.cost;
            }
            LOG("Received " << routing_updates.size() << " updates from:" << src->router_id);
            recompute_routing_table();
        }
    }
}

void timeout_handler() {
    if (rs.update_interval != 0) {
        mark_inactive();
        update_counter += CLOCK_TICK;
        if (update_counter == (rs.update_interval * 1000000)) {
            send_routing_updates();
            connect_data_ports();
            update_counter = 0;
        }
    }
}

void mark_inactive() {
    timeval now;
    gettimeofday(&now, NULL);
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        if (rs.routers[i].status == ACTIVE && rs.routers[i].type == NEIGHBOUR) {
            timeval expected_at = (struct timeval) {rs.routers[i].last_update.tv_sec + (3 * rs.update_interval),
                                                    rs.routers[i].last_update.tv_usec};
            if (now.tv_sec >= expected_at.tv_sec) {
                rs.routers[i].status = INACTIVE;
                LOG("Router " << rs.routers[i].router_id << " timed-out(" <<
                    util::time_str(rs.routers[i].last_update.tv_sec) << "L :" <<
                    util::time_str(now.tv_sec) << "N : " << util::time_str(expected_at.tv_sec) << "E)");
                rs.routers[i].cost = INF;
                distance_vectors[self->router_id][rs.routers[i].router_id] = INF;
                routing_table[rs.routers[i].router_id] = (struct route) {INF, INF};
                recompute_routing_table();
            }
        }
    }
}

void recompute_routing_table() {
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        router &destination = rs.routers[i];
        if (destination.type != SELF && destination.status != INACTIVE) {
            uint16_t min_cost = INF;
            uint16_t min_next_hop = INF;
            for (std::vector<router>::size_type j = 0; j != rs.routers.size(); j++) {
                router &neighbour = rs.routers[j];
                if (neighbour.type == NEIGHBOUR) {
                    uint16_t self_to_neighbour = distance_vectors[self->router_id][neighbour.router_id];
                    uint16_t neighbour_to_destination = distance_vectors[neighbour.router_id][destination.router_id];
                    // INF addition wraps around and restarts from 0!!!!!
                    if (self_to_neighbour != INF && neighbour_to_destination != INF) {
                        uint16_t cost = self_to_neighbour + neighbour_to_destination;
                        if (std::min(min_cost, cost) != min_cost) {
                            min_cost = cost;
                            min_next_hop = neighbour.router_id;
                        }
                    }
                }
            }
            routing_table[destination.router_id] = (struct route) {min_cost, min_next_hop};
        }
    }
}

router *find_by_port_ip(uint16_t port, uint32_t ip) {
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        struct router r = rs.routers[i];
        if (r.router_port == port && r.ip == ip) {
            return &rs.routers[i];
        }
    }
    return NULL;
}

router *find_by_id(uint16_t id) {
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        if (rs.routers[i].router_id == id) {
            return &rs.routers[i];
        }
    }
    return NULL;
}

route *route_for_destination(uint32_t ip) {
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        struct router r = rs.routers[i];
        if (r.ip == ip) {
            LOG("Route for " << r.router_id << " : " << r.ip_str << " via: " <<
                routing_table[r.router_id].next_hop_id << " cost: " <<
                routing_table[r.router_id].cost);
            return &routing_table[r.router_id];
        }
    }
    return NULL;
}

char *routing_payload() {
    char *payload = new char[ROUTING_UPDATE_HEADER_SIZE + (rs.router_count * ROUTING_UPDATE_ENTRY_SIZE)];
    bzero(payload, ROUTING_UPDATE_HEADER_SIZE + (rs.router_count * ROUTING_UPDATE_ENTRY_SIZE));
    struct ROUTING_UPDATE_HEADER *header = (struct ROUTING_UPDATE_HEADER *) payload;
    header->update_size = htons(rs.router_count);
    header->src_port = htons(self->router_port);
    header->src_ip_addr = self->ip;

    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        struct router r = rs.routers[i];
        struct ROUTING_UPDATE_ENTRY *entry = (struct ROUTING_UPDATE_ENTRY *) (payload + ROUTING_UPDATE_HEADER_SIZE +
                                                                              (i * ROUTING_UPDATE_ENTRY_SIZE));
        entry->ip_addr = r.ip;
        entry->port = htons(r.router_port);
        entry->padding = htons(0);
        entry->id = htons(r.router_id);
        entry->cost = htons(routing_table[r.router_id].cost);
    }
    return payload;
}

const routers parse_init(char *buffer) {
    unsigned offset = 0;
    rs.router_count = util::ntohui16(buffer, offset);
    rs.update_interval = util::ntohui16(buffer, offset);
    for (int i = 0; i < rs.router_count; ++i) {
        struct router r;
        r.router_id = util::ntohui16(buffer, offset);
        r.router_port = util::ntohui16(buffer, offset);
        r.data_port = util::ntohui16(buffer, offset);
        r.cost = util::ntohui16(buffer, offset);
        r.ip = util::toui32(buffer, offset);
        r.ip_str = std::string(inet_ntoa(*(struct in_addr *) &r.ip));

        r.status = INITIALIZED;
        r.data_socket_fd = 0;
        if (r.cost >= INF) {
            r.type = NON_NEIGHBOUR;
        }
        else if (r.cost == 0 && r.ip_str.compare(util::primary_ip().c_str()) == 0) {
            r.type = SELF;
        } else {
            r.type = NEIGHBOUR;
        }
        rs.routers.push_back(r);
    }
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        if (rs.routers[i].type == SELF) {
            self = &rs.routers[i];
        }
    }
    // init distance_vectors's to INF, except diagonals
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        for (std::vector<router>::size_type j = 0; j != rs.routers.size(); j++) {
            if (i == j) {
                distance_vectors[rs.routers[i].router_id][rs.routers[j].router_id] = 0;
            } else {
                distance_vectors[rs.routers[i].router_id][rs.routers[j].router_id] = INF;
            }
        }
    }
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        router r = rs.routers[i];
        switch (r.type) {
            case NEIGHBOUR: {
                distance_vectors[self->router_id][r.router_id] = r.cost;
                routing_table[r.router_id] = (struct route) {r.cost, self->router_id};
                break;
            }
            case SELF: {
                distance_vectors[self->router_id][r.router_id] = 0;
                routing_table[r.router_id] = (struct route) {0, self->router_id};
                break;
            }
            case NON_NEIGHBOUR: {
                distance_vectors[self->router_id][r.router_id] = INF;
                routing_table[r.router_id] = (struct route) {INF, INF};
                break;
            }
        }
    }
    recompute_routing_table();
    return rs;
}

const router *get_self() {
    return self;
}

void routing_table_response(int controller_fd) {
    uint16_t payload_len, response_len;
    char *cntrl_response_header, *cntrl_response_payload, *cntrl_response;

    payload_len = (uint16_t) (routing_table.size() * ROUTING_TABLE_ENTRY_SIZE);

    cntrl_response_header = create_response_header(controller_fd, controller::ROUTING_TABLE, 0, payload_len);

    if (payload_len != 0) {
        cntrl_response_payload = new char[payload_len];
        bzero(cntrl_response_payload, payload_len);
        int offset = 0;
        for (std::map<uint16_t, route>::iterator it = routing_table.begin(); it != routing_table.end(); ++it) {
            struct ROUTING_TABLE_ENTRY *entry = (struct ROUTING_TABLE_ENTRY *) (cntrl_response_payload +
                                                                                (offset * ROUTING_TABLE_ENTRY_SIZE));
            entry->id = htons(it->first);
            entry->padding = htons(0);
            entry->next_hop_id = htons(it->second.next_hop_id);
            entry->cost = htons(it->second.cost);
            ++offset;
        }
    } else {
        sendALL(controller_fd, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
        delete[](cntrl_response_header);
    }

    response_len = CNTRL_RESP_HEADER_SIZE + payload_len;
    cntrl_response = new char[response_len];
    bzero(cntrl_response, response_len);
    /* Copy Header */
    memcpy(cntrl_response, cntrl_response_header, CNTRL_RESP_HEADER_SIZE);
    delete[](cntrl_response_header);
    /* Copy Payload */
    memcpy(cntrl_response + CNTRL_RESP_HEADER_SIZE, cntrl_response_payload, payload_len);
    delete[](cntrl_response_payload);
    sendALL(controller_fd, cntrl_response, response_len);
    delete[](cntrl_response);
}

void update_cost(int controller_fd, char *payload) {
    char *cntrl_response = create_response_header(controller_fd, controller::UPDATE, 0, 0);
    sendALL(controller_fd, cntrl_response, CNTRL_RESP_HEADER_SIZE);
    free(cntrl_response);
    unsigned offset = 0;
    uint16_t id = util::ntohui16(payload, offset);
    uint16_t cost = util::ntohui16(payload, offset);
    rs.routers[id].cost = cost;
    distance_vectors[self->router_id][id] = cost;
//    routing_table[id] = (struct route) {INF, INF}; // TODO: need this?
    recompute_routing_table();
}

void connect_data_ports() {
    for (std::vector<router>::size_type i = 0; i != rs.routers.size(); i++) {
        if (rs.routers[i].status != INACTIVE
            && rs.routers[i].type == NEIGHBOUR
            && rs.routers[i].data_socket_fd == 0) {
            if (util::connect_to(&rs.routers[i].data_socket_fd, rs.routers[i].ip_str.c_str(),
                                 util::to_port_str(rs.routers[i].data_port).c_str(),
                                 SOCK_STREAM)) {

            } else {
                ERROR("Cannot connect to data port " << rs.routers[i].data_port << " of router by id: " <<
                      rs.routers[i].router_id);
            }
        }
    }
}







