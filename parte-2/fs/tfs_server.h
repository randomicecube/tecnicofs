#ifndef TFS_SERVER_H
#define TFS_SERVER_H

#include "common/common.h"
#include "state.h"
#include "config.h"
#include "state.h"
#include <sys/types.h>

/*
 * Performs the bridge between server and client in the tfs_mount operation
 */
void case_mount(char *request);

/*
 * Performs the bridge between server and client in the tfs_unmount operation
 */
void case_unmount(char *request);

/*
 * Performs the bridge between server and client in the tfs_open operation
 */
void case_open(char *request);

/*
 * Performs the bridge between server and client in the tfs_close operation
 */
void case_close(char *request);

/*
 * Performs the bridge between server and client in the tfs_write operation
 */
void case_write(char *request);

/*
 * Performs the bridge between server and client in the tfs_read operation
 */
void case_read(char *request);

/*
 * Performs the bridge between server and client in the tfs_shutdown operation
 */
void case_shutdown(char *request);

/*
 * Starts all the available sessions in the server, initializing:
 * - each session's lock
 * - each session's cond_var
 */
void start_sessions();
/*
  * Stops all the available sessions in the server, destroying:
  * - each session's lock
  * - each session's cond_var
  */
void end_sessions();

#endif