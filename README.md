# Software Defined Routing with data transfers #

Implements Distance Vector routing over a network of simulated routers with link costs with file data routing.

[Spec](https://github.com/wasifaleem/DV-Routing-with-multiple-file-transfers-flows/blob/experiments/PA3.pdf)

* Periodic routing updates are sent over UDP to maintain routing tables.
* Files are packetized and routed via TCP after consulting routing tables to find mincost routes.
* Supports multiple file transfer flows.
* Remove timed-out routers from network.
* Packets are dropped on TTL expiry.
* Implements controller to send control commands to routers.
* Use `select` syscall to accept input commands and monitor sockets at the same time(single-threaded).
