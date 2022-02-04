#ifndef STATE_H
#define STATE_H

#include "config.h"
#include "../common/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

/*
 * Directory entry
 */
typedef struct {
    char d_name[MAX_FILE_NAME];
    int d_inumber;
} dir_entry_t;

typedef enum { T_FILE, T_DIRECTORY } inode_type;

/*
 * I-node
 */
typedef struct {
    inode_type i_node_type;
    size_t i_size;
    int i_data_block;
    /* in a real FS, more fields would exist here */
} inode_t;

typedef enum { FREE = 0, TAKEN = 1 } allocation_state_t;

/*
 * Open file entry (in open file table)
 */
typedef struct {
    int of_inumber;
    size_t of_offset;
} open_file_entry_t;

#define MAX_DIR_ENTRIES (BLOCK_SIZE / sizeof(dir_entry_t))

/*
 * The regular pipe read and write functions aren't guaranteed to read/write
 * the number of bytes we want. Therefore, below are two functions which aim to
 * guarantee that the number of bytes we want are read/written.
 */

int read_buffer(int rx, char *buf, size_t to_read);
int write_buffer(int tx, char *buf, size_t to_write);

void lock_mutex(pthread_mutex_t *mutex);
void unlock_mutex(pthread_mutex_t *mutex);
void init_mutex(pthread_mutex_t *mutex);
void destroy_mutex(pthread_mutex_t *mutex);

void state_init();
void state_destroy();

int inode_create(inode_type n_type);
int inode_delete(int inumber);
inode_t *inode_get(int inumber);

int clear_dir_entry(int inumber, int sub_inumber);
int add_dir_entry(int inumber, int sub_inumber, char const *sub_name);
int find_in_dir(int inumber, char const *sub_name);

int data_block_alloc();
int data_block_free(int block_number);
void *data_block_get(int block_number);

int add_to_open_file_table(int inumber, size_t offset);
int remove_from_open_file_table(int fhandle);
open_file_entry_t *get_open_file_entry(int fhandle);

/* Stores the number of currently open files - useful for the function
 * tfs_destroy_after_all_closed() */
extern int open_files_count;

/* Condition variable and mutex for the tfs_destroy_after_all_closed()
 * function - related to all files being closed (or not) */
extern pthread_cond_t open_files_cond;
extern pthread_mutex_t open_files_mutex;

/* Condition that assures that tfs_open can only be used when tfs_init()
 * has been called */
extern int open_flag;

#endif // STATE_H
