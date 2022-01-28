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
    int rx; // pipe which the client reads to
    int tx; // pipe which the client writes to
    int session_id;
    char *pipename;
} Client;

typedef struct Pipe_men {
    int session_id;
    
    char opcode;
    
    char *name;
    
    int flags;
    
    int fhandle;
    
    size_t len;
    
    char *buffer;

} Pipe_men;

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
    ssize_t ret;
    bool shutting_down = false;
    do {
        Pipe_men message;
        printf("Actively waiting for a message...\n");
        printf("rx is %d\n", rx);
        ret = read(rx, &message, sizeof(Pipe_men));
        op_code = message.opcode;
        printf("Message received.\n");
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
            // TODO - HANDLE CASE WHERE NEXT SESSION ID IS OVER 64
            case TFS_OP_CODE_MOUNT:
                printf("[INFO]: Received TFS_OP_CODE_MOUNT\n");
                case_mount(rx);
                printf("[INFO]: Mounted\n");
                break;
            case TFS_OP_CODE_UNMOUNT:
                printf("[INFO]: Received TFS_OP_CODE_UNMOUNT\n");
                case_unmount(rx);
                printf("[INFO]: Unmounted\n");
                break;
            case TFS_OP_CODE_OPEN:
                printf("[INFO]: Received TFS_OP_CODE_OPEN\n");
                case_open(rx);
                printf("[INFO]: Opened\n");
                break;
            case TFS_OP_CODE_CLOSE:
                printf("[INFO]: Received TFS_OP_CODE_CLOSE\n");
                case_close(rx);
                printf("[INFO]: Closed\n");
                break;
            case TFS_OP_CODE_WRITE:
                printf("[INFO]: Received TFS_OP_CODE_WRITE\n");
                case_write(rx);
                printf("[INFO]: Wrote\n");
                break;
            case TFS_OP_CODE_READ:
                printf("[INFO]: Received TFS_OP_CODE_READ\n");
                case_read(rx);
                printf("[INFO]: Read\n");
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                printf("[INFO]: Received TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED\n");
                case_shutdown(rx);
                printf("[INFO]: Shut down\n");
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

int send_msg(int tx, Pipe_men message){
    ssize_t ret;
    ret = write(tx, &message, sizeof(Pipe_men));
    return check_errors_write(ret);
}

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

int read_msg(int rx, Pipe_men *message){
    ssize_t ret;
    ret = read(rx, message, sizeof(Pipe_men));
    return check_errors_read(ret);
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
    Pipe_men message;
    puts("brah");
    read_msg_pipename(rx, client_pipename);
    pipename_length = (ssize_t) strlen(client_pipename);
    printf("tf\n");
    for (ssize_t i = pipename_length; i < BUFFER_SIZE - 1; i++) {
        client_pipename[i] = '\0';
    }
    printf("server1\n");
    tx = open(client_pipename, O_WRONLY);
    printf("%s\n", client_pipename);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("server2\n");
    message.session_id = next_session_id;
    send_msg(tx, message);
    printf("server3\n");
    clients[next_session_id - 1].tx = tx;
    clients[next_session_id - 1].rx = rx;
    clients[next_session_id - 1].session_id = next_session_id;
    clients[next_session_id - 1].pipename = client_pipename;
    next_session_id++;
}

void case_unmount(int rx) {
    // TODO - do we need to unlink the client pipe after it is closed? 
    Pipe_men message;
    int session_id;
    read_msg(rx, &message);
    session_id = message.session_id;
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
    Pipe_men message;
    int session_id, flags;
    char filename[BUFFER_SIZE];
    ssize_t filename_length;
    read_msg(rx, &message);
    session_id = message.session_id;
    flags = message.flags;
    strcpy(filename, message.name);
    filename_length = (ssize_t) strlen(filename);
    for (ssize_t i = filename_length; i < BUFFER_SIZE - 1; i++) {
        filename[i] = '\0';
    }
    int call_ret = tfs_open(filename, flags);
    printf("[INFO]: tfs_open returned %d\n", call_ret);
    printf("[INFO]: Opened %s\n", filename);
    send_msg_int(clients[session_id - 1].tx, call_ret);
}

void case_close(int rx) {
    int session_id, fhandle;
    Pipe_men message;

    read_msg(rx, &message);
    session_id = message.session_id;
    fhandle = message.fhandle;

    int tfs_ret_int = tfs_close(fhandle);
    send_msg_int(clients[session_id - 1].tx, tfs_ret_int);
}

void case_write(int rx) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    Pipe_men message;
    read_msg(rx, &message);
    session_id = message.session_id;
    fhandle = message.fhandle;
    len = message.len;
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    strcpy(buffer, message.buffer);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    send_msg_ssize_t(clients[session_id - 1].tx, tfs_ret_ssize_t);
    free(buffer);
}

void case_read(int rx) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    Pipe_men message;
    read_msg(rx, &message);
    session_id = message.session_id;
    fhandle = message.fhandle;
    len = message.len;
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("len is %zu\n", len);
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    printf("tfs_ret_ssize_t is %zd\n", tfs_ret_ssize_t);
    send_msg_ssize_t(clients[session_id - 1].tx, tfs_ret_ssize_t);
    free(buffer);
}

void case_shutdown(int rx) {
    int session_id, tfs_ret_int;
    Pipe_men message;
    read_msg(rx, &message);
    session_id = message.session_id;
    tfs_ret_int = tfs_destroy_after_all_closed();
    send_msg_int(clients[session_id - 1].tx, tfs_ret_int);
}