#ifndef GLOBAL_H
#define GLOBAL_H

#include <Util.h>
#include <iomanip>

#define EMPTY_STRING ""
#define BACKLOG 10
#define CNTRL_HEADER_SIZE 8
#define CNTRL_RESP_HEADER_SIZE 8
#define INF ((uint16_t) INFINITY)


#endif //GLOBAL_H

#ifdef DEBUG
#define LOG(str) do { std::cout << util::log_time()  << "T " << std::setw(4) << __LINE__  << "L " << str << '\n'; } while( false )
#define ERROR(str) do { std::cout << "\033[31;1m" << util::log_time()  << "T " << std::setw(4) << __LINE__  << "L " << str <<"\033[0m" << '\n'; } while( false )
#else
#define LOG(str) do { } while ( false )
#define ERROR(str) do { } while ( false )
#endif