/**
 * @control_header_lib
 * @author  Swetank Kumar Saha <swetankk@buffalo.edu>
 * @version 1.0
 *
 * @section LICENSE
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details at
 * http://www.gnu.org/copyleft/gpl.html
 *
 * @section DESCRIPTION
 *
 * Routines to parse/generated control/control-response headers.
 */

#include <string.h>
#include <netinet/in.h>
#include <malloc.h>

#include "../include/global.h"
#include "../include/control_header_lib.h"

char *create_response_header(int sock_index, uint8_t control_code, uint8_t response_code, uint16_t payload_len) {
    char *buffer;

    struct CONTROL_RESPONSE_HEADER *cntrl_resp_header;
    struct sockaddr_in addr;
    socklen_t addr_size;

    buffer = (char *) malloc(sizeof(char) * CNTRL_RESP_HEADER_SIZE);
    cntrl_resp_header = (struct CONTROL_RESPONSE_HEADER *) buffer;

    addr_size = sizeof(struct sockaddr_in);
    getpeername(sock_index, (struct sockaddr *) &addr, &addr_size);

    /* Controller IP Address */
    memcpy(&(cntrl_resp_header->controller_ip_addr), &(addr.sin_addr), sizeof(struct in_addr));
    /* Control Code */
    cntrl_resp_header->control_code = control_code;
    /* Response Code */
    cntrl_resp_header->response_code = response_code;
    /* Payload Length */
    cntrl_resp_header->payload_len = htons(payload_len);

    return buffer;
}