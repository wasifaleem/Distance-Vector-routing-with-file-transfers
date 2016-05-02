#ifndef NETWORK_UTIL_H_
#define NETWORK_UTIL_H_

ssize_t recvALL(int sock_index, char *buffer, ssize_t nbytes);
ssize_t sendALL(int sock_index, char *buffer, ssize_t nbytes);

ssize_t recvFromALL(int sock_index, char *buffer, ssize_t nbytes, int flags, struct sockaddr_in * addr);
ssize_t sendToALL(int sock_index, char *buffer, ssize_t nbytes, struct addrinfo * addr);
#endif