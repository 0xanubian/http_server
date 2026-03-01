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
    READING_HEADERS,
    HEADERS_COMPLETE,
    READING_BODY,
    BODY_COMPLETE,
    PROCESSING,
    ERROR,
};

struct header_metadata_t {
    long content_length;
    int cl_count;
    bool is_chunked;
};

bool is_header_complete(char *header, uint32_t header_read_size, uint32_t start_index) {
    for (uint32_t i = start_index; (i + 3) < header_read_size; i++) {
        if (header[i] == '\r' &&
            header[i+1] == '\n' &&
            header[i+2] == '\r' &&
            header[i+3] == '\n') return true;
    }
    return false;
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

long search_cl(char* header, uint32_t *index, uint32_t header_read_size) {
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

int search_te(char* header, uint32_t *index, uint32_t header_read_size) {
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
            return 1;
        }
    }
    return 0;
}
struct header_metadata_t shallow_search(char *header, uint32_t header_read_size) {
    struct header_metadata_t metadata = {0};
    //long cl = 0;
    uint32_t index = 0;
    //int cl_count = 0;
    //int te_count = 0;
    while (index < header_read_size) {
        while (header[index] == ' ') index++;
        if (header[index] == 'c' || header[index] == 'C') {
            metadata.content_length = search_cl(header, &index, header_read_size);
            if (metadata.content_length) (metadata.cl_count)++;
        }
        if (header[index] == 't' || header[index] == 'T')
            metadata.is_chunked += search_te(header, &index, header_read_size);

        else skip_to_nline(header, &index, header_read_size);
        printf("Index: %d\n", index);
    }

    printf("\ncontent-length parsed: %ld\n", metadata.content_length);
    
    return metadata;
}

int main(int argc, char* argv[]) {
    struct sockaddr_in addr; //addr for bind

    
    int socket_file_des = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_file_des < 0) {
        perror("[!]socket() failed!!!");
        exit(-1);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    socklen_t addr_len = sizeof(addr);

    int bind_ret = bind(socket_file_des, (const struct sockaddr *)&addr, addr_len);

    if (bind_ret < 0) {
        perror("[!]bind() failed!!!");
        exit(-1);
    }

    int listen_ret = listen(socket_file_des, MAX_QUEUE_LEN);

    if (listen_ret < 0) {
        perror("[!]listen() failed!!!");
        exit(-1);
    }
    
    printf("Ready to accept\n");

    while(1) {
        int accept_fd = accept(socket_file_des, (struct sockaddr *)&addr, &addr_len);

        if (accept_fd < 0) {
            perror("[!]accept() failed!!!");
            exit(-1);
        }
        char *header = (char*)malloc(HEADER_SIZE);
        if (header == NULL) {
            perror("[!]malloc() failed...");
            close(accept_fd);
            continue;
        }

        enum http_state_t machine_state = READING_HEADERS;
        struct header_metadata_t metadata = {0};

        uint append_size = 0; 
        uint32_t header_read_size = 0; //to count how many bytes we have read yet
        
        while (header_read_size < HEADER_SIZE) {
            size_t remaining = HEADER_SIZE - header_read_size;
            ssize_t recv_ret = recv(accept_fd, (header+append_size), remaining, 0);
            
            if (recv_ret < 0) {
                perror("[!] recv() failed!!!");
                exit(-1);
            }
            
            append_size += recv_ret;
            uint32_t start_pointer_for_header_completion_scan = (header_read_size >= 3) ? header_read_size - 3 : 0;
            header_read_size += recv_ret;

            bool header_flag = is_header_complete(header, header_read_size, start_pointer_for_header_completion_scan);
            if (header_flag && machine_state == READING_HEADERS) machine_state = HEADERS_COMPLETE;

            if (header_flag && machine_state == HEADERS_COMPLETE) {
                printf("\nHeader complete....\n");
                printf("header size: %d\n", header_read_size);

                //search for content-length and transfer-encoding
                metadata = shallow_search(header, header_read_size);
                if (metadata.cl_count > 1) {
                    printf("\nerror 400: bad request(multiple content-length found)\n\n");
                    break;
                }
                if (metadata.cl_count && metadata.is_chunked) {
                    printf("\nboth content length and transfer-encoding: chunked found\n\n");
                    //break;
                }
                if (metadata.content_length < 0) {
                    printf("cl: %ld\nInvalid content-length\n", metadata.content_length);
                    break;
                }
                if (metadata.is_chunked) printf("\nthe request has chunked transfer-encoding\n\n");
                if (metadata.content_length <= 0 && !metadata.is_chunked) {
                    printf("error 411: length required\n");
                    break;
                }

                machine_state = READING_BODY;
            }

            if (recv_ret == 0 && !header_flag && machine_state == READING_HEADERS) {
                printf("\nConnection closed but header is incomplete\ncan't parse the header....\nclosing the connection\n");
                close(accept_fd);
                break;
            }

            if (recv_ret == 0 && header_flag && machine_state == READING_BODY) {
                printf("\nconnection closed...\nheader is complete but body is incomplete\ncan't process the req...\nclosing the connection\nif both is complete process req and send response.\n");
                close(accept_fd);
                break;
            }

            write(1, header, header_read_size);            
            //header and its shenanigans are complete. its time to read body 
            if(machine_state == READING_BODY) break;
        }
        
        if (machine_state == READING_BODY && !metadata.is_chunked) {
            char *body_buffer = (char*)malloc(metadata.content_length);
            if (body_buffer == NULL) {
                perror("[!] malloc failed for body...\n");
                continue;
            }

            ssize_t body_bytes_read = 0;
            while (body_bytes_read < metadata.content_length) {
                ssize_t recv_ret = recv(accept_fd, body_buffer+body_bytes_read, metadata.content_length - body_bytes_read, 0);
                body_bytes_read += recv_ret;
            }
        }

        close(accept_fd);
    }
    close(socket_file_des);
    return 0;
}
