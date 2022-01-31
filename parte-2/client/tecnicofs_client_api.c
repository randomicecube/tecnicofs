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
    printf("[LOG]: Establishing connection with server...\n");
    client.tx = open(server_pipe_path, O_WRONLY);
    printf("[LOG]: Connection established.\n");
    printf("tx: %d\n", client.tx);

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
    memcpy(server_request + 1, client_pipe_path, sizeof(char) * strlen(client_pipe_path));

    // TODO -> WRITE ALL 40 BYTES, INCLUDING \0
    printf("[LOG]: Sending request to server...\n");
    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    printf("[LOG]: Request sent.\n");
    printf("[LOG]: Waiting for response from server...\n");
    if (read(client.rx, &client.session_id, sizeof(int)) == -1) {
        return -1;
    }
    printf("[LOG]: Response received.\n");
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
    return 0;
}

int tfs_open(char const *name, int flags) {
    int ret;
    char server_request[
        sizeof(char) + 2 * sizeof(int) + sizeof(char) * BUFFER_SIZE
    ];
    char op_code = TFS_OP_CODE_OPEN;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id, sizeof(int));
    memcpy(server_request + 1 + sizeof(int), &flags, sizeof(int));
    memset(server_request + 1 + 2 * sizeof(int), '\0', sizeof(char) * BUFFER_SIZE);
    memcpy(server_request + 1 + 2 * sizeof(int), name, sizeof(char) * strlen(name));
    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    if (read(client.rx, &ret, sizeof(int)) == -1) {
        return -1;
    }
    return ret;
}

int tfs_close(int fhandle) {
    int ret;
    char server_request[
        sizeof(char) + 2 * sizeof(int)
    ];
    char op_code = TFS_OP_CODE_CLOSE;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id, sizeof(int));
    memcpy(server_request + 1 + sizeof(int), &fhandle, sizeof(int));

    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    if (read(client.rx, &ret, sizeof(int)) == -1) {
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
    if (read(client.rx, &ret, sizeof(ssize_t)) == -1) {
        return -1;
    }
    return ret;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    ssize_t ret;
    char server_request[
        sizeof(char) + 2 * sizeof(int) + sizeof(size_t)
    ];
    char op_code = TFS_OP_CODE_READ;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id, sizeof(int));
    memcpy(server_request + 1 + sizeof(int), &fhandle, sizeof(int));
    memcpy(server_request + 1 + 2 * sizeof(int), &len, sizeof(size_t));

    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    if (read(client.rx, &ret, sizeof(ssize_t)) == -1) {
        return -1;
    }
    if (read(client.rx, buffer, sizeof(char) * len) == -1) {
        return -1;
    }
    return ret;
}

int tfs_shutdown_after_all_closed() {
    int shutdown_ret;
    char server_request[
        sizeof(char) + sizeof(int)
    ];
    char op_code = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;
    memcpy(server_request, &op_code, sizeof(char));
    memcpy(server_request + 1, &client.session_id, sizeof(int));

    if (write(client.tx, server_request, sizeof(server_request)) == -1) {
        return -1;
    }
    if (read(client.rx, &shutdown_ret, sizeof(int)) == -1) {
        return -1;
    }
    // TODO - DO WE NEED TO CLOSE AND UNLINK HERE?
    return shutdown_ret;
}