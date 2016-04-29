#ifndef GLOBAL_H
#define GLOBAL_H

#define AUTHOR_PAYLOAD "I, wasifale, have read and understood the course academic integrity policy."
#define HOSTNAME_LEN 128
#define PATH_LEN 256
#define EMPTY_STRING ""
#define BACKLOG 10
#define CONTROLLER_MSG_BUFFER 1000
#define CONTROLLER_HEADER_LEN 8
#define PKT_WIDTH 4

#endif //GLOBAL_H

#ifdef DEBUG
#define LOG(str) do { std::cout << __LINE__  << ": " << str << std::endl; } while( false )
#define ERROR(str) do { std::cout << "\033[1;1m" << __LINE__ << ": "  <<  str << "\033[0m" << std::endl; } while( false )
#else
#define LOG(str) do { } while ( false )
#define ERROR(str) do { } while ( false )
#endif