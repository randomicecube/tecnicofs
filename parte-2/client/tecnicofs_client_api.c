#include "tecnicofs_client_api.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

Client client; // each client is singular for each process

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    if (unlink(client_pipe_path) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client_pipe_path,
                strerror(errno));
        return -1;
    }
    if (mkfifo(client_pipe_path, 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        return -1;
    }
    client.rx = open(client_pipe_path, O_RDWR);
    if (client.rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return -1;
    }
    client.tx = open(server_pipe_path, O_WRONLY);

    if (client.tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return -1;
    }

    Pipe_men message;
    message.opcode = TFS_OP_CODE_MOUNT;

    printf("sending message\n");
    if (send_msg(client.tx, message) == -1) {
        return -1;
    }

    printf("After message, before pipe");
    if (send_msg_str(client.tx, client_pipe_path) == -1){
        return -1;
    }
    printf("After pipe");

    // the server returns the session id
    read_msg(client.rx, &message);
    client.session_id = message.session_id;

    strcpy(client.pipename, client_pipe_path);
    return 0;
}

int tfs_unmount() {
    Pipe_men message;
    message.opcode = TFS_OP_CODE_UNMOUNT;
    message.session_id = client.session_id;
    
    if (send_msg(client.tx, message) == -1) {
        return -1;
    }

    if (close(client.rx) == -1) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        return -1;
    }
    if (close(client.tx) == -1) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        return -1;
    }
    if (unlink(client.pipename) != 0) {
        fprintf(stderr, "[ERR]: unlink failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int tfs_open(char const *name, int flags) {
    int ack;
    Pipe_men message;
    message.opcode = TFS_OP_CODE_OPEN;
    message.session_id = client.session_id;
    strcpy(message.name, name);
    printf("strlen(name) = %zu\n", strlen(name));
    printf("message.name is -%s-\n", message.name);
    message.flags = flags;
    printf("[INFO]: open %s\n", name);
    if (send_msg(client.tx, message) == -1){
        return -1;
    }
    printf("[INFO]: open sent\n");

    if (read_msg_int(client.rx, &ack) == -1) {
        return -1;
    }
    printf("never gets here\n");
    return ack;
}

int tfs_close(int fhandle) {
    int ret;

    Pipe_men message;
    message.opcode = TFS_OP_CODE_CLOSE;
    message.session_id = client.session_id;
    message.fhandle = fhandle;

    if (send_msg(client.tx, message) == -1) {
        return -1;
    }
    if (read_msg_int(client.rx, &ret) == -1) {
        return -1;
    }
    return ret;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    ssize_t ret;

    Pipe_men message;
    message.opcode = TFS_OP_CODE_WRITE;
    message.session_id = client.session_id;
    message.len = len;
    message.fhandle = fhandle;
    strcpy(message.buffer, buffer);
    printf("sending %zu bytes\n", message.len);

    if (send_msg(client.tx, message) == -1) {
        return -1;
    }

    if (read_msg_ssize_t(client.rx, &ret) == -1) {
        return -1;
    }
    printf("[INFO]: write %zu bytes\n", ret);
    printf("[INFO]: Checking again: %zu\n", ret);
    return ret;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    ssize_t ret;

    Pipe_men message;
    message.opcode = TFS_OP_CODE_READ;
    message.session_id = client.session_id;
    message.fhandle = fhandle;
    message.len = len;
    strcpy(message.buffer, buffer); // NAO SEI SE ISTO ESTA BEM

    if (send_msg(client.tx, message) == -1) {
        return -1;
    }

    if (read_msg_ssize_t(client.rx, &ret) == -1) {
        return -1;
    }
    return ret;
}

int tfs_shutdown_after_all_closed() {
    int shutdown_ack;

    Pipe_men message;
    message.opcode = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    message.session_id = client.session_id;

    if (send_msg(client.tx, message) == -1) {
        return -1;
    }

    if (read_msg_int(client.rx, &shutdown_ack) == -1) {
        return -1;
    }
    // TODO - DO WE NEED TO CLOSE AND UNLINK HERE?
    return shutdown_ack;
}

int send_msg(int tx, Pipe_men message){
    ssize_t ret;
    ssize_t bytes_to_be_written = sizeof(Pipe_men);
    while (bytes_to_be_written > 0) {
        ret = write(tx, &message, sizeof(Pipe_men));
        bytes_to_be_written -= ret;
    }
    return check_errors_write(ret);
}

/*
int send_msg_opcode(int tx, char opcode) {
    ssize_t ret;
    ret = write(tx, &opcode, sizeof(char));
    return check_errors_write(ret);
}

int send_msg_int(int tx, int arg) {
    ssize_t ret;
    ret = write(tx, &arg, sizeof(int));
    return check_errors_write(ret);
}

int send_msg_size_t(int tx, size_t arg) {
    ssize_t ret;
    ret = write(tx, &arg, sizeof(size_t));
    return check_errors_write(ret);
}
*/

int send_msg_str(int tx, char const* buffer){
    ssize_t ret;
    ret = write(tx, buffer, sizeof(char)*strlen(buffer));
    return check_errors_write(ret);
}

int send_msg_pipename(int tx, char* pipename) {
    ssize_t ret;
    ret = write(tx, pipename, sizeof(char) * (BUFFER_SIZE - 1));
    check_errors_read(ret);
    return 0;
}

int read_msg(int rx, Pipe_men *message){
    ssize_t ret;
    ssize_t bytes_to_be_read = sizeof(Pipe_men);
    while (bytes_to_be_read > 0) {
        ret = read(rx, message, sizeof(Pipe_men));
        bytes_to_be_read -= ret;
    }
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

int read_msg_ssize_t(int rx, ssize_t *arg) {
    ssize_t ret;
    ret = read(rx, arg, sizeof(size_t));
    return check_errors_read(ret);
}

int check_errors_write(ssize_t ret) {
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int check_errors_read(ssize_t ret) {
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}