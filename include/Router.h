#ifndef ROUTER_H
#define ROUTER_H

#include <global.h>
#include <stdlib.h>
#include <netdb.h>
#include <vector>
#include <iostream>
#include "controller.h"


class Router {
public:
    Router(const char *control_port) : control_port(control_port) {
    }

    void start();

    static void set_router_socket(int router_fd);

    static void set_data_socket(int data_fd);

private:
    const char *control_port;

    void select_loop();
};

#endif //ROUTER_H
