#include "tecnicofs_client_api.h"
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

typedef struct Client {
    int rx; // pipe which the client reads to
    int tx; // pipe which the client writes to
    int session_id;
    char *pipename;
} Client;

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
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_MOUNT;
    
    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_str(client.tx, client_pipe_path)
    ret = write(client.tx, client_pipe_path, sizeof(char) * (strlen(client_pipe_path) + 1));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;
    
    // the server returns the session id
    ret = read(client.rx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return -1;
    }
    client.pipename = client_pipe_path;
    return 0;
}

int tfs_unmount() {
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_UNMOUNT;
    
    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;
    
    //send_msg_int(client.tx, client.sessiond_id);
    ret = write(client.tx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;
    
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
    } // TODO - do we need to unlink the client pipe here?
    return 0;
}

int tfs_open(char const *name, int flags) {
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_OPEN;
    
    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, client.sessiond_id);
    ret = write(client.tx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_str(client.tx, &name);
    ret = write(client.tx, &name, sizeof(char)*strlen(name));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, flags);
    ret = write(client.tx, &flags, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    return 0;
}

int tfs_close(int fhandle) {
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_CLOSE;

    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, client.sessiond_id);
    ret = write(client.tx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, fhandle);
    ret = write(client.tx, &fhandle, sizeof(int));    
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    return 0;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_WRITE;

    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, client.sessiond_id);
    ret = write(client.tx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;
    
    //send_msg_int(client.tx, fhandle);
    ret = write(client.tx, &fhandle, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;
    
    //send_msg_ssizet(client.tx, len);
    ret = write(client.tx, &len, sizeof(ssize_t));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;
    
    //send_msg_str(client.tx, &buffer); - este nao pq é sizeof(char)*len e nao strlen
    ret = write(client.tx, &buffer, sizeof(char)*len);
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    return 0;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_READ;

    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, client.sessiond_id);
    ret = write(client.tx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, fhandle);
    ret = write(client.tx, &fhandle, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_ssizet(client.tx, len);
    ret = write(client.tx, &len, sizeof(ssize_t));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    return 0;
}

int tfs_shutdown_after_all_closed() {
    ssize_t ret;
    ssize_t ack = 0;
    char buf[BUFFER_SIZE];
    char op_code = TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED;

    //send_msg_opcode(client.tx, &op_code);
    ret = write(client.tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    //send_msg_int(client.tx, client.sessiond_id);
    ret = write(client.tx, &client.session_id, sizeof(int));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ack = read(client.rx, buf, BUFFER_SIZE - 1);
    buf[ack] = 0;

    // TODO - DO WE NEED TO CLOSE AND UNLINK HERE?
    return 0;
}

// TODO - SEND ACKNLOWGEMENET INFORMATION FROM THE SERVER
// SEEMS MAINLY IMPORTANT FOR THE SHUTDOWN AFTER ALL CLOSED BIT
// ALSO FOR READ AND WRITE IT'S NEEDED (SUPPOSED TO RETURN 
// THE NUMBER OF BYTES WRITTEN/READ)

/*void send_msg_opcode(int tx, char const* buffer){
    ssize_t ret;
    ret = write(tx, buffer, sizeof(char));
    check_errors(ret);
}

void send_msg_str(int tx, char const* buffer){
    ssize_t ret;
    ret = write(tx, buffer, sizeof(char)*strlen(buffer));
    check_errors(ret);
}

void send_msg_int(int tx, int arg){
    ssize_t ret;
    ret = write(tx, &arg, sizeof(int));
    check_errors(ret);
}

void send_msg_ssizet(int tx, ssize_t arg){
    ssize_t ret;
    ret = write(tx, arg, sizeof(ssize_t));
    check_errors(ret);
}

void check_errors(ssize_t ret){
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_SUCCESS);
    }
}*/