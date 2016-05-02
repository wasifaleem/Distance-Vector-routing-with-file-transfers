#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include <string>
#include <vector>

namespace controller {
    enum message_type {
        AUTHOR, INIT, ROUTING_TABLE, UPDATE, CRASH, SENDFILE, SENDFILE_STATS, LAST_DATA_PACKET, PENULTIMATE_DATA_PACKET
    };

    int do_bind_listen(const char *control_port);

    int accept(int controller_fd);

    bool receive(int controller_fd);

    void close_fd(int controller_fd);

    bool is_set(int controller_fd);

}
#endif //CONTROLLER_H
