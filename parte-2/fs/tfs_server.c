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

#define BUFFER_SIZE (40)
#define MAX_CLIENTS (64)

typedef struct Client {
    int rx; // pipe which the client reads to
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
    if (unlink(pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create pipe
    if (mkfifo(pipename, 0777) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // open pipe for reading
    int rx = open(pipename, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char op_code;
    ssize_t ret;
    bool shutting_down = false;
    do {
        ret = read(rx, &op_code, sizeof(char));
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (ret == 0) {
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        // printf for debug purposes - TODO delete it
        printf("[INFO]: Received op_code %c\n", op_code);

        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                case_mount(rx);
                break;
            case TFS_OP_CODE_UNMOUNT:
                case_unmount(rx);
                break;
            case TFS_OP_CODE_OPEN:
                case_open(rx);
                break;
            case TFS_OP_CODE_CLOSE:
                case_close(rx);
                break;
            case TFS_OP_CODE_WRITE:
                case_write(rx);
                break;
            case TFS_OP_CODE_READ:
                case_read(rx);
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                case_shutdown(rx);
                shutting_down = true;
                break;
            default: 
                fprintf(stderr, "[ERR]: Invalid op code: %d\n", op_code);
                break;
        }
    } while(!shutting_down);

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
    for (ssize_t i = ret; i < BUFFER_SIZE; i++) {
        pipename[i] = '\0';
    }
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

void case_mount(int rx) {
    ssize_t pipename_length;
    char client_pipename[BUFFER_SIZE];
    int tx;
    read_msg_pipename(rx, client_pipename);
    pipename_length = (ssize_t) strlen(client_pipename);
    for (ssize_t i = pipename_length; i < BUFFER_SIZE - 1; i++) {
        client_pipename[i] = '\0';
    }
    if (unlink(client_pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client_pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (mkfifo(client_pipename, 0777) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    tx = open(client_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    send_msg_int(tx, next_session_id);
    clients[next_session_id - 1].tx = tx;
    clients[next_session_id - 1].rx = rx;
    clients[next_session_id - 1].session_id = next_session_id;
    clients[next_session_id - 1].pipename = client_pipename;
    next_session_id++;
}

void case_unmount(int rx) {
    // TODO - do we need to unlink the client pipe after it is closed? 
    int session_id;
    read_msg_int(rx, &session_id);
    if (close(clients[session_id - 1].tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (close(clients[session_id - 1].rx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (unlink(clients[session_id - 1].pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", clients[session_id - 1].pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void case_open(int rx) {
    int session_id, flags;
    char filename[BUFFER_SIZE];
    ssize_t filename_length;
    read_msg_int(rx, &session_id);
    read_msg_pipename(rx, filename);
    filename_length = (ssize_t) strlen(filename);
    for (ssize_t i = filename_length; i < BUFFER_SIZE - 1; i++) {
        filename[i] = '\0';
    }
    read_msg_int(rx, &flags);
    int call_ret = tfs_open(filename, flags);
    send_msg_int(clients[session_id - 1].tx, call_ret);
}

void case_close(int rx) {
    int session_id, fhandle;
    read_msg_int(rx, &session_id);
    read_msg_int(rx, &fhandle);
    int tfs_ret_int = tfs_close(fhandle);
    send_msg_int(clients[session_id - 1].tx, tfs_ret_int);
}

void case_write(int rx) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    read_msg_int(rx, &session_id);
    read_msg_int(rx, &fhandle);
    read_msg_int(rx, &fhandle);
    read_msg_size_t(rx, &len);
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    read_msg_str(rx, buffer, len);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    send_msg_ssize_t(clients[session_id - 1].tx, tfs_ret_ssize_t);
    free(buffer);
}

void case_read(int rx) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    read_msg_int(rx, &session_id);
    read_msg_int(rx, &fhandle);
    read_msg_size_t(rx, &len);
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    send_msg_ssize_t(clients[session_id - 1].tx, tfs_ret_ssize_t);
    free(buffer);
}

void case_shutdown(int rx) {
    int session_id, tfs_ret_int;
    read_msg_int(rx, &session_id);
    tfs_ret_int = tfs_destroy_after_all_closed();
    send_msg_int(clients[session_id - 1].tx, tfs_ret_int);
}