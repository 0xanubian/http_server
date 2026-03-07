#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#define HEADER_SIZE  8388608 // 8mb

uint16_t PORT = 8080;
int MAX_QUEUE_LEN = 5; //max length of queue for the pending connection for socket_file_des. see man listen
size_t SIZE = 512; // size for read

enum http_state_t {
    INIT,
    READING_HEADERS,
    HEADERS_COMPLETE,
    READING_BODY,
    BODY_COMPLETE,
    PROCESSING,
    ERROR,
};

struct connection_t {
    struct sockaddr_in addr;
    socklen_t addr_len;
    //ssize_t recv_ret;
    int socket_file_des;
    //int ret;
    int accept_fd;
};

struct http_req_t {
    char *header;
    size_t remaining;
    uint append_size;
    uint32_t header_read_size;
    uint32_t start_pointer_for_header_completion_scan;
    bool header_flag;
    long content_length;
    int cl_count;
    bool is_chunked;
};

enum http_state_t machine_state = INIT;

void is_header_complete(struct http_req_t *req) {
    for (uint32_t i = req->start_pointer_for_header_completion_scan; (i + 3) < req->header_read_size; i++) {
        if ((req->header)[i] == '\r' &&
            (req->header)[i+1] == '\n' &&
            (req->header)[i+2] == '\r' &&
            (req->header)[i+3] == '\n') {
            
            req->header_flag = true;
            return;
        }
    }
    return;
}
void skip_to_nline(char* header, uint32_t *index, uint32_t header_read_size) {
    for (uint32_t i = *index; i < header_read_size; i++) {
        if (header[i] == '\r' && header[i+1] == '\n') {
            (*index) += 2;
            return;
        }
        else (*index)++;
    }
}

long search_cl(char* header, uint32_t header_read_size, uint32_t *index) {
    if (tolower(header[*index]) == 'c' &&
        tolower(header[(*index)+1]) == 'o' &&
        tolower(header[(*index)+2]) == 'n' &&
        tolower(header[(*index)+3]) == 't' &&
        tolower(header[(*index)+4]) == 'e' &&
        tolower(header[(*index)+5]) == 'n' &&
        tolower(header[(*index)+6]) == 't' &&
        header[(*index)+7] == '-' &&
        tolower(header[(*index)+8]) == 'l' &&
        tolower(header[(*index)+9]) == 'e' &&
        tolower(header[(*index)+10]) == 'n' &&
        tolower(header[(*index)+11]) == 'g' &&
        tolower(header[(*index)+12]) == 't' &&
        tolower(header[(*index)+13]) == 'h') {
        *index = (*index) + 14;
        //header += 14;
        
        //skip white spaces and : and parse the content-length
        while (header[*index] == ' ' || header[*index] == ':') {
            //header++;
            (*index)++;
        }
        printf("\nindex before cl parsing: %d\n", *index);
        printf("\nheader before cl parsing: %s\n", header+(*index));
        long content_length = atol(header+(*index));
        return content_length;
    }
    else {
        skip_to_nline(header, index, header_read_size);
        return 0;
    }
}

bool search_te(char* header, uint32_t header_read_size, uint32_t *index) {
    if (tolower(header[*index]) == 't' &&
        tolower(header[(*index)+1]) == 'r' &&
        tolower(header[(*index)+2]) == 'a' &&
        tolower(header[(*index)+3]) == 'n' &&
        tolower(header[(*index)+4]) == 's' &&
        tolower(header[(*index)+5]) == 'f' &&
        tolower(header[(*index)+6]) == 'e' &&
        tolower(header[(*index)+7]) == 'r' &&
        header[(*index)+8] == '-' &&
        tolower(header[(*index)+9]) == 'e' &&
        tolower(header[(*index)+10]) == 'n' &&
        tolower(header[(*index)+11]) == 'c' &&
        tolower(header[(*index)+12]) == 'o' &&
        tolower(header[(*index)+13]) == 'd' &&
        tolower(header[(*index)+14]) == 'i' &&
        tolower(header[(*index)+15]) == 'n' &&
        tolower(header[(*index)+16]) == 'g') {

        *index = (*index) + 17;
        //header += 17;

        while (header[*index] == ' ' || header[*index] == ':') (*index)++;

        if (tolower(header[*index]) == 'c' &&
            tolower(header[(*index)+1]) == 'h' &&
            tolower(header[(*index)+2]) == 'u' &&
            tolower(header[(*index)+3]) == 'n' &&
            tolower(header[(*index)+4]) == 'k' &&
            tolower(header[(*index)+5]) == 'e' &&
            tolower(header[(*index)+6]) == 'd' ) {
            
            *index = (*index) + 7;
            return true;
        }
    }
    return false;
}

void shallow_search(struct http_req_t *req) {
    long cl = 0;
    uint32_t index = 0;
    int cl_count = 0;
    bool is_chunked = false;

    char *header = req->header;
    uint32_t header_size = req->header_read_size;

    while (index < header_size) {
        while (header[index] == ' ') index++;
        if (header[index] == 'c' || header[index] == 'C') {
            cl = search_cl(header, header_size, &index);
            if (cl) (cl_count)++;
        }
        if (header[index] == 't' || header[index] == 'T')
            is_chunked = search_te(header, header_size, &index);

        else skip_to_nline(header, &index, header_size);
        printf("Index: %d\n", index);
    }

    printf("\ncontent-length parsed: %ld\n", cl);
    
    req->content_length = cl;
    req->cl_count = cl_count;
    req->is_chunked = is_chunked;
}

void socket_stuff(struct connection_t *connection) {
    connection->socket_file_des = socket(AF_INET, SOCK_STREAM, 0);

    if (connection->socket_file_des < 0) {
        perror("[!]socket() failed!!!");
        exit(EXIT_FAILURE);
    }

    connection->addr.sin_family = AF_INET;
    connection->addr.sin_addr.s_addr = INADDR_ANY;
    connection->addr.sin_port = htons(PORT);

    connection->addr_len = sizeof(connection->addr);

    int ret = bind(connection->socket_file_des, (const struct sockaddr *)&(connection->addr), connection->addr_len);

    if (ret < 0) {
        perror("[!]bind() failed!!!");
        exit(EXIT_FAILURE);
    }

    ret = listen(connection->socket_file_des, MAX_QUEUE_LEN);

    if (ret < 0) {
        perror("[!]listen() failed!!!");
        exit(EXIT_FAILURE);
    }
    
    printf("Ready to accept\n");
    return;
}

struct http_req_t* http_header_read(struct connection_t *connection) {
    char *iheader = (char*)malloc(HEADER_SIZE);
    if (iheader == NULL) {
        perror("[!]malloc() failed...");
        close(connection->accept_fd);
        return NULL;
    }

    machine_state = READING_HEADERS;

    struct http_req_t *req = (struct http_req_t*)malloc(sizeof(struct http_req_t));
    *req = (struct http_req_t){0};
    req->header = iheader;

    while (req->header_read_size < HEADER_SIZE) {
        req->remaining = HEADER_SIZE - req->header_read_size;
        ssize_t recv_ret = recv(connection->accept_fd, ((req->header)+(req->append_size)), req->remaining, 0);

        if (recv_ret < 0) {
            perror("[!] recv() failed!!!");
            return NULL;
        }

        req->append_size += recv_ret;
        req->start_pointer_for_header_completion_scan = (req->header_read_size >= 3) ? req->header_read_size - 3 : 0;
        req->header_read_size += recv_ret;

        is_header_complete(req);
        if (req->header_flag && machine_state == READING_HEADERS) machine_state = HEADERS_COMPLETE;

        if (req->header_flag && machine_state == HEADERS_COMPLETE) {
            printf("\nHeader complete....\n");
            printf("header size: %d\n", req->header_read_size);

            //search for content-length and transfer-encoding
            shallow_search(req);
            if (req->cl_count > 1) {
                printf("\nerror 400: bad request(multiple content-length found)\n\n");
                break;
            }
            if (req->cl_count && req->is_chunked) {
                printf("\nboth content length and transfer-encoding: chunked found\n\n");
                //break;
            }
            if (req->content_length < 0) {
                printf("cl: %ld\nInvalid content-length\n", req->content_length);
                break;
            }
            if (req->is_chunked) printf("\nthe request has chunked transfer-encoding\n\n");
            if (req->content_length <= 0 && !(req->is_chunked)) {
                printf("error 411: length required\n");
                break;
            }

            machine_state = READING_BODY;
        }

        if (recv_ret == 0 && !(req->header_flag) && machine_state == READING_HEADERS) {
            printf("\nConnection closed but header is incomplete\ncan't parse the header....\nclosing the connection\n");
            close(connection->accept_fd);
            break;
        }

        if (recv_ret == 0 && req->header_flag && machine_state == READING_BODY) {
            printf("\nconnection closed...\nheader is complete but body is incomplete\ncan't process the req...\nclosing the connection\nif both is complete process req and send response.\n");
            close(connection->accept_fd);
            break;
        }

        write(1, req->header, req->header_read_size);            
        //header and its shenanigans are complete. its time to read body 
        if(machine_state == READING_BODY) break;
    }
    return req;
}

int main(int argc, char* argv[]) {
    struct connection_t *connection = (struct connection_t*)malloc(sizeof(struct connection_t));
    if (connection == NULL) {
        perror("[!] malloc failed for connection_t");
        exit(EXIT_FAILURE);
    }

    socket_stuff(connection);

    while(1) {
        connection->accept_fd = accept(connection->socket_file_des,
                                       (struct sockaddr *)&(connection->addr),
                                       &(connection->addr_len));

        if (connection->accept_fd < 0) {
            perror("[!]accept() failed!!!");
            continue;
        }

        struct http_req_t *req = http_header_read(connection);
        if(req == NULL) {
            printf("\n\n[!]Fatal error while reading header\n\n");
            continue;
        }
        
        if (machine_state == READING_BODY && !(req->is_chunked)) {
            char *body_buffer = (char*)malloc(req->content_length);
            if (body_buffer == NULL) {
                perror("[!] malloc failed for body...\n");
                continue;
            }
            printf("Reached body reading\n\n");
            break; //delete this
            //ssize_t body_bytes_read = 0;
            //while (body_bytes_read < req->content_length) {
            //    ssize_t recv_ret = recv(connection->accept_fd, body_buffer+body_bytes_read, req->content_length - body_bytes_read, 0);
            //    body_bytes_read += recv_ret;
            //}
        }

        close(connection->accept_fd);
    }
    close(connection->socket_file_des);
    return 0;
}
