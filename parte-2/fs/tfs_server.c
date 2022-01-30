#include "operations.h"
#include "tfs_server.h"
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
#include <pthread.h>

typedef struct Session{
    int session_id;
    bool is_active = false;
    pthread_mutex_t session_lock;
    pthread_cond_t session_flag;
    pthread_t session_t;
    char *buffer;
    int tx;
    char *pipename;
} Session;

int next_session_id = 1;
Session sessions[MAX_CLIENTS];
bool shutting_down = false;

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    tfs_init();

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    // unlink pipe
    if (unlink(pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
    // create pipe
    if (mkfifo(pipename, 0640) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    // open pipe for reading
    int rx = open(pipename, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    start_sessions();
    char *client_request = malloc(MAX_REQUEST_SIZE);
    if (client_request == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    ssize_t ret;
    do {
        ret = read(rx, client_request, sizeof(char) * MAX_REQUEST_SIZE);
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            free(client_request);
            end_sessions();
            exit(EXIT_FAILURE);
        }
        if (ret == 0) { // reached EOF (client closed pipe)
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                free(client_request);
                end_sessions();
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while(!shutting_down);

    free(client_request);
    end_sessions();
    close(rx);
    unlink(pipename);
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Below are the functions that implement each operation (server-side).
 * ----------------------------------------------------------------------------
 */

void case_mount(char *request) {
    char client_pipename[BUFFER_SIZE];
    int tx;
    lock_mutex(&sessions[next_session_id-1].session_lock);
    memcpy(client_pipename, request + 1, sizeof(char) * BUFFER_SIZE);
    tx = open(client_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[next_session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (write(tx, &next_session_id, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[next_session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    sessions[next_session_id - 1].tx = tx;
    sessions[next_session_id - 1].session_id = next_session_id;
    sessions[next_session_id - 1].pipename = client_pipename;
    unlock_mutex(&sessions[next_session_id-1].session_lock);
    next_session_id++;
    
}

void case_unmount(char *request) {
    int session_id;
    memcpy(&session_id, request + 1, sizeof(int));
    lock_mutex(&sessions[session_id-1].session_lock);
    if (close(sessions[session_id - 1].tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (unlink(sessions[session_id - 1].pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", sessions[session_id - 1].pipename,
                strerror(errno));
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&sessions[session_id-1].session_lock);
}

void case_open(char *request) {
    int session_id, flags;
    char filename[BUFFER_SIZE];
    memcpy(&session_id, request + 1, sizeof(int));
    lock_mutex(&sessions[session_id-1].session_lock);
    memcpy(&flags, request + 1 + sizeof(int), sizeof(int));
    memcpy(filename, request + 1 + 2 * sizeof(int), sizeof(char) * BUFFER_SIZE);
    int call_ret = tfs_open(filename, flags);
    if (write(sessions[session_id - 1].tx, &call_ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&sessions[session_id-1].session_lock);
}

void case_close(char *request) {
    int session_id, fhandle;
    memcpy(&session_id, request + 1, sizeof(int));
    lock_mutex(&sessions[session_id-1].session_lock);
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    int tfs_ret_int = tfs_close(fhandle);
    if (write(sessions[session_id - 1].tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&sessions[session_id-1].session_lock);
}

void case_write(char *request) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&session_id, request + 1, sizeof(int));
    lock_mutex(&sessions[session_id-1].session_lock);
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    memcpy(&len, request + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    memcpy(buffer, request + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    if (write(sessions[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&sessions[session_id-1].session_lock);
    free(buffer);
}

void case_read(char *request) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&session_id, request + 1, sizeof(int));
    lock_mutex(&sessions[session_id-1].session_lock);
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    memcpy(&len, request + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    if (write(sessions[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (write(sessions[session_id - 1].tx, buffer, (size_t) tfs_ret_ssize_t) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        unlock_mutex(&sessions[session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&sessions[session_id-1].session_lock);
    free(buffer);
}

void case_shutdown(char *request) {
    int session_id, tfs_ret_int;
    memcpy(&session_id, request + 1, sizeof(int));
    lock_mutex(&sessions[session_id-1].session_lock);
    tfs_ret_int = tfs_destroy_after_all_closed();
    if (write(sessions[session_id - 1].tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        unlock_mutex(&sessions[next_session_id-1].session_lock);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&sessions[session_id-1].session_lock);
}

/*
 * ----------------------------------------------------------------------------
 * Below are the session initialization/destruction functions.
 * ----------------------------------------------------------------------------
 */

void start_sessions() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].session_id = i + 1;
        init_mutex(&sessions[i].session_lock);
        if (pthread_cond_init(&sessions[i].session_flag, NULL) != 0) {
            fprintf(stderr, "[ERR]: cond init failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (pthread_create(&sessions[i].session_t, NULL, thread_handler, (void *) &sessions[i]) != 0) {
            fprintf(stderr, "[ERR]: thread create failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

void end_sessions() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_mutex_destroy(&sessions[i].session_lock);
        pthread_cond_destroy(&sessions[i].session_flag);
    }
}

/*
 * ----------------------------------------------------------------------------
 * Below is the thread handler function.
 * ----------------------------------------------------------------------------
 */

void *thread_handler(void *arg) {
    Session session = *(Session *) arg;
    char op_code;
    while (true) {
        lock_mutex(&session.session_lock);
        while (session.is_active == false) {
            pthread_cond_wait(&session.session_flag, &session.session_lock);
        }
        session.is_active = true;
        memcpy(&op_code, session.buffer, sizeof(char));
        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                case_mount(session.buffer);
                break;
            case TFS_OP_CODE_UNMOUNT:
                case_unmount(session.buffer);
                break;
            case TFS_OP_CODE_OPEN:
                case_open(session.buffer);
                break;
            case TFS_OP_CODE_CLOSE:
                case_close(session.buffer);
                break;
            case TFS_OP_CODE_WRITE:
                case_write(session.buffer);
                break;
            case TFS_OP_CODE_READ:
                case_read(session.buffer);
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                case_shutdown(session.buffer);
                shutting_down = true;
                break;
            default:
                fprintf(stderr, "[ERR]: invalid request: %c\n", op_code);
        }
        session.is_active = false;
        pthread_cond_signal(&session.session_flag);
        unlock_mutex(&session.session_lock);
    }
}