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

    char server_request[
        sizeof(char) + BUFFER_SIZE * sizeof(char)
    ];
    char op_code = TFS_OP_CODE_MOUNT;
    memcpy(server_request, &op_code, sizeof(char));
    memset(server_request + 1, '\0', sizeof(char) * BUFFER_SIZE);
    memcpy(server_request + 1, client_pipe_path,
           sizeof(char) * strlen(client_pipe_path));

    // TODO -> WRITE ALL 40 BYTES, INCLUDING \0
    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    // clear the buffer
    // memset(server_request, '\0', sizeof(char) * MAX_REQUEST_SIZE);

    read(client.rx, &client.session_id, sizeof(int));
    return 0;
}

int tfs_unmount() {
    char server_request[
        sizeof(char) + sizeof(int)
    ];
    char op_code = TFS_OP_CODE_UNMOUNT;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id, sizeof(int));
    
    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
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
    char server_request[
        sizeof(char) + 2 * sizeof(int) + sizeof(char) * BUFFER_SIZE
    ];
    char op_code = TFS_OP_CODE_OPEN;
    printf("[INFO]: Starting open procedures in client API\n");
    memcpy(server_request, &op_code, sizeof(char));
    // printf("strlen(server_request) is %ld\n", strlen(server_request));
    // printf("server_request is %s\n", server_request);
    memcpy(server_request + 1, &client.session_id, sizeof(int));
    // printf("strlen(server_request) is %ld\n", strlen(server_request));
    // printf("server_request is %s\n", server_request);
    memcpy(server_request + 1 + sizeof(int), &flags, sizeof(int));
    // printf("strlen(server_request) is %ld\n", strlen(server_request));
    // printf("server_request is %s\n", server_request);
    memset(server_request + 1 + 2 * sizeof(int), '\0', sizeof(char) * BUFFER_SIZE);
    memcpy(server_request + 1 + 2 * sizeof(int), name, sizeof(char) * strlen(name));
    printf("strlen(server_request) is %ld\n", strlen(server_request));
    printf("server_request is %s\n", server_request);
    printf("[INFO]: open %s\n", name);
    if (write(client.tx, server_request, sizeof(server_request)) == -1){
        return -1;
    }
    printf("[INFO]: open sent\n");

    if (read_msg_int(client.rx, &ack) == -1) {
        return -1;
    }
    return ack;
}

int tfs_close(int fhandle) {
    int ret;
    char server_request[
        sizeof(char) + 2 * sizeof(int)
    ];
    char op_code = TFS_OP_CODE_CLOSE;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id,
           sizeof(int));
    memcpy(server_request + 1 + sizeof(int), &fhandle, sizeof(int));

    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    if (read_msg_int(client.rx, &ret) == -1) {
        return -1;
    }
    return ret;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    ssize_t ret;
    char server_request[
        sizeof(char) + 2 * sizeof(int) + sizeof(char) * len + sizeof(size_t)
    ];
    char op_code = TFS_OP_CODE_WRITE;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id, sizeof(int));
    memcpy(server_request + 1 + sizeof(int), &fhandle, sizeof(int));
    memcpy(server_request + 1 + 2 * sizeof(int), &len, sizeof(size_t));
    memcpy(server_request + 1 + 2 * sizeof(int) + sizeof(size_t), buffer, sizeof(char) * len);

    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
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
    char server_request[
        sizeof(char) + 2 * sizeof(int) + sizeof(size_t)
    ];
    char op_code = TFS_OP_CODE_READ;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id,
           sizeof(int));
    memcpy(server_request + 1 + sizeof(int), &fhandle, sizeof(int));
    memcpy(server_request + 1 + 2 * sizeof(int), &len, sizeof(size_t));

    // todo - send back buffer
    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }

    if (read_msg_ssize_t(client.rx, &ret) == -1) {
        return -1;
    }
    if (read_msg(client.rx, buffer) == -1) {
        return -1;
    }
    return ret;
}

int tfs_shutdown_after_all_closed() {
    int shutdown_ack;
    char server_request[
        sizeof(char) + sizeof(int)
    ];
    char op_code = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id,
           sizeof(int));

    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }

    if (read_msg_int(client.rx, &shutdown_ack) == -1) {
        return -1;
    }
    // TODO - DO WE NEED TO CLOSE AND UNLINK HERE?
    return shutdown_ack;
}

int send_msg(int tx, char *request){
    ssize_t ret;
    ret = write(tx, request, sizeof(request));
    return check_errors_write(ret);
}

int read_msg(int rx, char *response){
    ssize_t ret;
    ret = read(rx, response, sizeof(response));
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