#include "operations.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_CLIENTS (64)

typedef struct Client {
    int tx; // pipe which the client writes to
    int session_id;
    char *pipename;
} Client;

int next_session_id = 1;
Client clients[MAX_CLIENTS];

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    // unlink pipe
    printf("Unlinking pipe %s...\n", pipename);
    if (unlink(pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("Creating pipe %s...\n", pipename);
    // create pipe
    if (mkfifo(pipename, 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    printf("Opening pipe %s for reading...\n", pipename);
    // open pipe for reading
    int rx = open(pipename, O_RDWR);
    printf("Checking if pipe %s is open...\n", pipename);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char op_code;
    char *client_request = malloc(MAX_REQUEST_SIZE); // TODO FREE
    if (client_request == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ssize_t ret;
    bool shutting_down = false;
    do {
        ret = read(rx, client_request, sizeof(char) * MAX_REQUEST_SIZE);
        printf("ret is %ld\n", ret);
        printf("Request's length is %ld\n", strlen(client_request));
        memcpy(&op_code, client_request, sizeof(char));
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            free(client_request);
            exit(EXIT_FAILURE);
        }
        if (ret == 0) {
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                free(client_request);
                exit(EXIT_FAILURE);
            }
        }

        // printf for debug purposes - TODO delete it
        printf("[INFO]: Received op_code %c\n", op_code);

        switch (op_code) {
            // TODO - HANDLE CASE WHERE NEXT SESSION ID IS OVER 64
            case TFS_OP_CODE_MOUNT:
                printf("[INFO]: Received TFS_OP_CODE_MOUNT\n");
                case_mount(client_request);
                printf("[INFO]: Mounted\n");
                break;
            case TFS_OP_CODE_UNMOUNT:
                printf("[INFO]: Received TFS_OP_CODE_UNMOUNT\n");
                case_unmount(client_request);
                printf("[INFO]: Unmounted\n");
                break;
            case TFS_OP_CODE_OPEN:
                printf("[INFO]: Received TFS_OP_CODE_OPEN\n");
                case_open(client_request);
                printf("[INFO]: Opened\n");
                break;
            case TFS_OP_CODE_CLOSE:
                printf("[INFO]: Received TFS_OP_CODE_CLOSE\n");
                case_close(client_request);
                printf("[INFO]: Closed\n");
                break;
            case TFS_OP_CODE_WRITE:
                printf("[INFO]: Received TFS_OP_CODE_WRITE\n");
                case_write(client_request);
                printf("[INFO]: Wrote\n");
                break;
            case TFS_OP_CODE_READ:
                printf("[INFO]: Received TFS_OP_CODE_READ\n");
                case_read(client_request);
                printf("[INFO]: Read\n");
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                printf("[INFO]: Received TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED\n");
                case_shutdown(client_request);
                printf("[INFO]: Shut down\n");
                shutting_down = true;
                break;
            default: 
                fprintf(stderr, "[ERR]: Invalid op code: %d\n", op_code);
                break;
        }
    } while(!shutting_down);

    free(client_request);
    close(rx);
    unlink(pipename);
    return 0;
}

// send_msg and check errors functions

int send_msg_opcode(int tx, char opcode) {
    ssize_t ret;
    ret = write(tx, &opcode, sizeof(char));
    return check_errors_write(ret);
}

int send_msg_str(int tx, char const* buffer){
    ssize_t ret;
    ret = write(tx, buffer, sizeof(char)*strlen(buffer));
    return check_errors_write(ret);
}

int send_msg_int(int tx, int arg) {
    ssize_t ret;
    ret = write(tx, &arg, sizeof(int));
    return check_errors_write(ret);
}

int send_msg_ssize_t(int tx, ssize_t arg) {
    ssize_t ret;
    ret = write(tx, &arg, sizeof(ssize_t));
    return check_errors_write(ret);
}


int read_msg_pipename(int rx, char* pipename) {
    ssize_t ret;
    ret = read(rx, pipename, sizeof(char) * BUFFER_SIZE - 1);
    check_errors_read(ret);
    return 0;
}

int read_msg_str(int rx, char *buffer, size_t len) {
    ssize_t ret; 
    ret = read(rx, buffer, sizeof(char) * len);
    return check_errors_read(ret);
}

int read_msg_int(int rx, int *arg) {
    ssize_t ret;
    ret = read(rx, arg, sizeof(int));
    return check_errors_read(ret);
}

int read_msg_size_t(int rx, size_t *arg) {
    ssize_t ret;
    ret = read(rx, arg, sizeof(size_t));
    return check_errors_read(ret);
}

int check_errors_write(ssize_t ret) {
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return 0;
}

int check_errors_read(ssize_t ret) {
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    return 0;
}

// functions utilized for each op code case

void case_mount(char *request) {
    char client_pipename[BUFFER_SIZE];
    for (size_t i = 0; i < BUFFER_SIZE - 1; i++) {
        client_pipename[i] = '\0';
    }
    int tx;
    printf("[INFO]: Mounting...\n");
    printf("Request's size: %lu\n", strlen(request));
    memcpy(client_pipename, request + sizeof(char), sizeof(char) * BUFFER_SIZE);
    printf("before anything, client_pipename: %s\n", client_pipename);
    printf("[INFO]: Pipe's length: %lu\n", strlen(client_pipename));
    printf("[INFO]: Mounting %s\n", client_pipename);
    tx = open(client_pipename, O_WRONLY);
    printf("[INFO]: Opened %s\n", client_pipename);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    send_msg_int(tx, next_session_id);
    clients[next_session_id - 1].tx = tx;
    clients[next_session_id - 1].session_id = next_session_id;
    clients[next_session_id - 1].pipename = client_pipename;
    next_session_id++;
}

void case_unmount(char *request) {
    // TODO - do we need to unlink the client pipe after it is closed? 
    int session_id;
    memcpy(&session_id, request + sizeof(char), sizeof(int));
    if (close(clients[session_id - 1].tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (unlink(clients[session_id - 1].pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", clients[session_id - 1].pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void case_open(char *request) {
    int session_id, flags;
    char filename[BUFFER_SIZE];
    ssize_t filename_length;
    printf("before session_id, filename, flags\n");
    memcpy(&session_id, request + 1, sizeof(int));
    printf("session_id: %d\n", session_id);
    memcpy(&flags, request + 1 + session_id, sizeof(int));
    printf("flags: %d\n", flags);
    memcpy(filename, request + 1 + session_id + flags, sizeof(char) * BUFFER_SIZE);
    printf("filename: %s\n", filename);
    filename_length = (ssize_t) strlen(filename);
    for (ssize_t i = filename_length; i < BUFFER_SIZE - 1; i++) {
        filename[i] = '\0';
    }
    printf("looking for -%s-\n", filename);
    printf("[INFO] just before tfs_open\n");
    int call_ret = tfs_open(filename, flags);
    printf("[INFO] just after tfs_open\n");
    printf("Call ret is %d\n", call_ret);
    send_msg_int(clients[session_id - 1].tx, call_ret);
    printf("[INFO] after sending int\n");
}

void case_close(char *request) {
    int session_id, fhandle;
    memcpy(&session_id, request + sizeof(char), sizeof(int));
    memcpy(&fhandle, request + sizeof(int), sizeof(int));
    int tfs_ret_int = tfs_close(fhandle);
    send_msg_int(clients[session_id - 1].tx, tfs_ret_int);
}

void case_write(char *request) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&session_id, request + sizeof(char), sizeof(int));
    memcpy(&fhandle, request + sizeof(int), sizeof(int));
    memcpy(&len, request + sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    memcpy(buffer, request + sizeof(size_t), sizeof(char) * len);
    printf("Trying to write %zu\n", len);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    printf("[INFO] tfs_write returned %ld\n", tfs_ret_ssize_t);
    send_msg_ssize_t(clients[session_id - 1].tx, tfs_ret_ssize_t);
    free(buffer);
}

void case_read(char *request) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&session_id, request + sizeof(char), sizeof(int));
    memcpy(&fhandle, request + sizeof(int), sizeof(int));
    memcpy(&len, request + sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("Trying to read %ld bytes\n", len);
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    send_msg_ssize_t(clients[session_id - 1].tx, tfs_ret_ssize_t);
    send_msg_str(clients[session_id - 1].tx, buffer);
    free(buffer);
}

void case_shutdown(char *request) {
    int session_id, tfs_ret_int;
    memcpy(&session_id, request + sizeof(char), sizeof(int));
    tfs_ret_int = tfs_destroy_after_all_closed();
    send_msg_int(clients[session_id - 1].tx, tfs_ret_int);
}