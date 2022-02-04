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

int next_session_id = 1;
int failure_code = -1;
Session sessions[MAX_CLIENTS];
bool shutting_down = false;
bool shutdown_called = false;
pthread_mutex_t next_session_id_lock;
pthread_mutex_t shutting_down_lock;

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    tfs_init();

    char *pipename = argv[1];
    printf("[INFO]: Starting TecnicoFS server with pipe called %s\n", pipename);

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

    ssize_t ret;
    int session_id;
    char op_code;
    char temp_buffer[MAX_REQUEST_SIZE];
    Session *current_session;
    bool too_many_clients;
    lock_mutex(&shutting_down_lock);
    do {
        too_many_clients = false;
        unlock_mutex(&shutting_down_lock);
        ret = read(rx, &op_code, sizeof(char));
        if (!check_pipe_open(ret, rx, pipename)) { // if it had to be reopened
            continue;
        }

        if (op_code == TFS_OP_CODE_MOUNT) {
            lock_mutex(&next_session_id_lock);
            session_id = next_session_id++;
            current_session = &sessions[session_id - 1];
            lock_mutex(&current_session->session_lock);
            current_session->session_id = session_id;
            memcpy(current_session->buffer, &op_code, sizeof(char));

            if (next_session_id > 64) {
                handle_too_many_clients(current_session);
                // we still need to read the rest of the content sent by the client
                // even though we are not going to do anything of use to it,
                // so that there are no conflicts with further clients' requests
                if (read_buffer(rx, temp_buffer, MOUNT_SIZE_SERVER) == -1) {
                    write(current_session->tx, &failure_code, sizeof(int));
                }
                unlock_mutex(&current_session->session_lock);
                continue;
            }
            if (read_buffer(rx, current_session->buffer + 1, MOUNT_SIZE_SERVER) == -1) {
                write(current_session->tx, &failure_code, sizeof(int));
                unlock_mutex(&current_session->session_lock);
                continue;
            }
            unlock_mutex(&next_session_id_lock);
        } else {
            if (read(rx, &session_id, sizeof(int)) == -1) {
                fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
                continue;
            }
            current_session = &sessions[session_id - 1];
            lock_mutex(&current_session->session_lock);
            if (session_id < 1 || session_id > MAX_CLIENTS) {
                fprintf(stderr, "[ERR]: invalid session id: %d\n", session_id);
                write(current_session->tx, &failure_code, sizeof(int));
                unlock_mutex(&current_session->session_lock);
                continue;
            }

            memcpy(current_session->buffer, &op_code, sizeof(char));
            memcpy(current_session->buffer + 1, &session_id, sizeof(int));
            switch (op_code) {
                case TFS_OP_CODE_OPEN:
                    if (read_buffer(rx, current_session->buffer + 1 + sizeof(int), OPEN_SIZE_SERVER) == -1) {
                        write(current_session->tx, &failure_code, sizeof(int));
                        unlock_mutex(&current_session->session_lock);
                        continue;
                    }
                    break;
                case TFS_OP_CODE_CLOSE:
                    if (read_buffer(rx, current_session->buffer + 1 + sizeof(int), CLOSE_SIZE_SERVER) == -1) {
                        write(current_session->tx, &failure_code, sizeof(int));
                        unlock_mutex(&current_session->session_lock);
                        continue;
                    } 
                    break;
                case TFS_OP_CODE_WRITE:
                    size_t len;
                    if (read(rx, current_session->buffer + 1 + sizeof(int), sizeof(int)) == -1) {
                        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
                        write(current_session->tx, &failure_code, sizeof(int));
                        unlock_mutex(&current_session->session_lock);
                        continue;
                    }
                    if (read(rx, &len, sizeof(size_t)) == -1) {
                        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
                        write(current_session->tx, &failure_code, sizeof(int));
                        unlock_mutex(&current_session->session_lock);
                        continue;
                    }
                    memcpy(current_session->buffer + 1 + 2 * sizeof(int), &len, sizeof(size_t));
                    if (read_buffer(rx, current_session->buffer + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len) == -1) {
                        write(current_session->tx, &failure_code, sizeof(int));
                        unlock_mutex(&current_session->session_lock);
                        continue;
                    }
                    break;
                case TFS_OP_CODE_READ:
                    if (read_buffer(rx, current_session->buffer + 1  + sizeof(int), READ_SIZE_SERVER) == -1) {
                        write(current_session->tx, &failure_code, sizeof(int));
                        unlock_mutex(&current_session->session_lock);
                        continue;
                    }
                    break;
                case TFS_OP_CODE_UNMOUNT:
                case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                    // cases not required - we have already read the session id
                    break;
                default:
                    // if unknown op code, we won't be able to know where to "skip to"
                    // therefore, we need to end the program here
                    fprintf(stderr, "[ERR]: Invalid op_code: %d\n", op_code);
                    write(current_session->tx, &failure_code, sizeof(int));
                    return 2;
            }
        }

        if (!too_many_clients) {
            // we only send the request if the session id was valid
            // otherwise we just ignore the request's content
            current_session->is_active = true;
            pthread_cond_signal(&current_session->session_flag);
        }
        unlock_mutex(&current_session->session_lock);
        lock_mutex(&shutting_down_lock);
    } while(!shutting_down);

    // should never get here: case_shutdown should end the program whenever it's finished
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Below are the functions that implement each operation (server-side).
 * ----------------------------------------------------------------------------
 */

void case_mount(Session *session) {
    char client_pipename[BUFFER_SIZE];
    int tx;
    memcpy(client_pipename, session->buffer + 1, sizeof(char) * BUFFER_SIZE);
    tx = open(client_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        exit(EXIT_FAILURE);
    }
    if (write(tx, &session->session_id, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        return;
    }
    session->tx = tx;
    session->pipename = client_pipename;
}

void case_unmount(Session *session) {
    int successful_unmount = 0;
    if (write(session->tx, &successful_unmount, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return;
    }
    if (close(session->tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        return;
    }
    if (unlink(session->pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", session->pipename,
                strerror(errno));
        return;
    }
}

void case_open(Session *session) {
    int flags;
    char filename[BUFFER_SIZE];
    memcpy(&flags, session->buffer + 1 + sizeof(int), sizeof(int));
    memcpy(filename, session->buffer + 1 + 2 * sizeof(int), sizeof(char) * BUFFER_SIZE);
    int call_ret = tfs_open(filename, flags);
    if (write(session->tx, &call_ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        return;
    }
}

void case_close(Session *session) {
    int fhandle;
    int ret;
    memcpy(&fhandle, session->buffer + 1 + sizeof(int), sizeof(int));
    ret = tfs_close(fhandle);
    if (write(session->tx, &ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        return;
    }
}

void case_write(Session *session) {
    int fhandle;
    size_t len;
    char *buffer;
    ssize_t ret;
    memcpy(&fhandle, session->buffer + 1 + sizeof(int), sizeof(int));
    memcpy(&len, session->buffer + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        exit(EXIT_FAILURE);
    }
    memcpy(buffer, session->buffer + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
    ret = tfs_write(fhandle, buffer, len);
    if (write(session->tx, &ret, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        free(buffer);
        return;
    }
    free(buffer);
}

void case_read(Session *session) {
    int fhandle;
    size_t len;
    char *buffer;
    ssize_t ret;
    memcpy(&fhandle, session->buffer + 1 + sizeof(int), sizeof(int));
    memcpy(&len, session->buffer + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        exit(EXIT_FAILURE);
    }
    ret = tfs_read(fhandle, buffer, len);
    if (write(session->tx, &ret, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        free(buffer);
        return;
    }
    if (write_buffer(session->tx, buffer, (size_t) ret) == -1) {
        write(session->tx, &failure_code, sizeof(int));
        free(buffer);
        return;
    }
    free(buffer);
}

void case_shutdown(Session *session) {
    int ret = tfs_destroy_after_all_closed();
    lock_mutex(&shutting_down_lock);
    shutting_down = true;
    if (write(session->tx, &ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        write(session->tx, &failure_code, sizeof(int));
        exit(EXIT_FAILURE);
    }
    unlock_mutex(&shutting_down_lock);
    
    printf("[INFO]: TecnicoFS server was shut down successfully.\n");
    exit(EXIT_SUCCESS);
}

/*
 * ----------------------------------------------------------------------------
 * Below are the session initialization/destruction functions.
 * ----------------------------------------------------------------------------
 */

void start_sessions() {
    init_mutex(&next_session_id_lock);
    init_mutex(&shutting_down_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].session_id = i + 1;
        sessions[i].is_active = false;
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

/*
 * ----------------------------------------------------------------------------
 * Below is the thread handler function.
 * ----------------------------------------------------------------------------
 */

void *thread_handler(void *arg) {
    Session *session = (Session *) arg;
    char op_code;
    while (true) {
        lock_mutex(&session->session_lock);
        while (!session->is_active) {
            pthread_cond_wait(&session->session_flag, &session->session_lock);
        }
        lock_mutex(&shutting_down_lock);
        session->is_active = true;
        memcpy(&op_code, session->buffer, sizeof(char));
        if (shutdown_called && op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
            // not continuing if already shutting down
            write(session->tx, &failure_code, sizeof(int));
            unlock_mutex(&shutting_down_lock);
            unlock_mutex(&session->session_lock);
            break;
        }
        if (op_code == TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED) {
            shutdown_called = true;
        }
        unlock_mutex(&shutting_down_lock);
        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                case_mount(session);
                break;
            case TFS_OP_CODE_UNMOUNT:
                case_unmount(session);
                break;
            case TFS_OP_CODE_OPEN:
                case_open(session);
                break;
            case TFS_OP_CODE_CLOSE:
                case_close(session);
                break;
            case TFS_OP_CODE_WRITE:
                case_write(session);
                break;
            case TFS_OP_CODE_READ:
                case_read(session);
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                case_shutdown(session);
                break;
            default: break; // never gets here, already treated in main
        }
        session->is_active = false;
        strcpy(session->buffer, ""); // clearing the buffer after each request
        unlock_mutex(&session->session_lock);
    }
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 * Below are general-use helper functions used in main.
 * ----------------------------------------------------------------------------
 */
bool check_pipe_open(ssize_t ret, int rx, char *pipename) {
    if (ret == -1) {
        fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
        return false;
    }
    if (ret == 0) { // reached EOF (client closed pipe)
        close(rx);
        rx = open(pipename, O_RDONLY);
        if (rx == -1) {
            fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        lock_mutex(&shutting_down_lock);
        if (shutting_down) {
            unlock_mutex(&shutting_down_lock);
            exit(EXIT_SUCCESS);
        }
        unlock_mutex(&shutting_down_lock);
        return false;
    }
    return true;
}

void handle_too_many_clients(Session *session) {
    fprintf(stderr, "[ERR]: Too many clients connected. Try again shortly.\n");
    unlock_mutex(&next_session_id_lock);
    char pipename[BUFFER_SIZE];
    memcpy(pipename, session->buffer + 1, BUFFER_SIZE);
    int rx;
    if ((rx = open(pipename, 0640)) == -1) {
        fprintf(stderr, "[ERR]: open failed %s\n", strerror(errno));
        return;
    }
    if (write(rx, &failure_code, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed %s\n", strerror(errno));
        return;
    }
    close(rx);
}
