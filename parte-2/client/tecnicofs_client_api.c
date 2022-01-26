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
    if (mkfifo(client_pipe_path, 0777) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        return -1;
    }
    client.rx = open(client_pipe_path, O_RDONLY);
    if (client.rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return -1;
    }
    client.tx = open(server_pipe_path, O_WRONLY);
    if (client.tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return -1;
    }
    char op_code = TFS_OP_CODE_MOUNT;
    
    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_str(client.tx, client_pipe_path) == -1) {
        return -1;
    }
    // the server returns the session id
    ssize_t ret = read(client.rx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    strcpy(client.pipename, client_pipe_path);
    return 0;
}

int tfs_unmount() {
    char op_code = TFS_OP_CODE_UNMOUNT;
    
    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, client.session_id) == -1) {
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
    int ret;
    char op_code = TFS_OP_CODE_OPEN;
    
    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, client.session_id) == -1) {
        return -1;
    }
    if (send_msg_str(client.tx, name) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, flags) == -1) {
        return -1;
    }
    ret = (int) read(client.rx, &ret, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    return ret;
}

int tfs_close(int fhandle) {
    int ret;
    char op_code = TFS_OP_CODE_CLOSE;

    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, client.session_id) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, fhandle) == -1) {
        return -1;
    }
    ret = (int) read(client.rx, &ret, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    return ret;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    ssize_t ret;
    char op_code = TFS_OP_CODE_WRITE;

    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, client.session_id) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, fhandle) == -1) {
        return -1;
    }
    if (send_msg_size_t(client.tx, len) == -1) {
        return -1;
    }
    if (send_msg_str(client.tx, buffer) == -1) {
        return -1;
    }
    ret = read(client.rx, &ret, sizeof(ssize_t));
    if (ret == -1)  {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    return ret;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    ssize_t ret;
    char op_code = TFS_OP_CODE_READ;

    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, client.session_id) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, fhandle) == -1) {
        return -1;
    }
    if (send_msg_str(client.tx, buffer) == -1) {
        return -1;
    }
    if (send_msg_size_t(client.tx, len) == -1) {
        return -1;
    }
    read(client.rx, &ret, sizeof(ssize_t));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    return ret;
}

int tfs_shutdown_after_all_closed() {
    int shutdown_ack;
    ssize_t ret;
    char op_code = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;

    if (send_msg_opcode(client.tx, op_code) == -1) {
        return -1;
    }
    if (send_msg_int(client.tx, client.session_id) == -1) {
        return -1;
    }
    ret = read(client.rx, &shutdown_ack, sizeof(int)); // error propagation from the server
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    // TODO - DO WE NEED TO CLOSE AND UNLINK HERE?
    return shutdown_ack;
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

int send_msg_size_t(int tx, size_t arg) {
    ssize_t ret;
    ret = write(tx, &arg, sizeof(size_t));
    return check_errors_write(ret);
}

int check_errors_write(ssize_t ret) {
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}