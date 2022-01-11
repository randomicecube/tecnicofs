#ifndef STATE_H
#define STATE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pthread.h>

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
    int i_data_block[MAX_DIRECT_BLOCKS];
    int i_indirect_data_block;
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

pthread_rwlock_t *get_inode_table_lock(int inumber);
pthread_rwlock_t *get_open_file_table_lock(int file_handle);

void lock_mutex(pthread_mutex_t *mutex);
void read_lock_rwlock(pthread_rwlock_t *rwlock);
void write_lock_rwlock(pthread_rwlock_t *rwlock);
void unlock_mutex(pthread_mutex_t *mutex);
void unlock_rwlock(pthread_rwlock_t *rwlock);
void init_mutex(pthread_mutex_t *mutex);
void init_rwlock(pthread_rwlock_t *rwlock);
void destroy_mutex(pthread_mutex_t *mutex);
void destroy_rwlock(pthread_rwlock_t *rwlock);

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

#endif // STATE_H
