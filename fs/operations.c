#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                if (data_block_free(inode->i_data_block) == -1) {
                    return -1;
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    size_t no_blocks = to_write / BLOCK_SIZE + 1;    
    size_t direct_blocks = (no_blocks > 10 ? 10 : no_blocks);

    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to write */
    if (to_write + file->of_offset > BLOCK_SIZE) {
        to_write = BLOCK_SIZE - file->of_offset;
    }

    if (to_write > 0) {
        if (inode->i_size == 0) {
            /* If empty file, allocate new block */
            if (allocate_empty_file(inode, direct_blocks, no_blocks) == -1) {
                return -1;
            }
	    }

        for (int i = 0; i < direct_blocks; i++){
            void *block = data_block_get(inode->i_data_block[i]);
            if (block == NULL) {
                return -1;
            }

            /* Perform the actual write */
            memcpy(block + file->of_offset, buffer, to_write);

            /* The offset associated with the file handle is
            * incremented accordingly */
            file->of_offset += to_write;
            if (file->of_offset > inode->i_size) {
                inode->i_size = file->of_offset;
            }
        }
        if (no_blocks > 10){
            int *indirect_block = (int *) data_block_get(inode->i_indirect_data_block);
            for (int j = 10; j < no_blocks; j++) {
                void *first_indirection_block = data_block_get(indirect_block[j]);
                memcpy(first_indirection_block + file->of_offset, buffer, to_write);
                file->of_offset += to_write;
                if (file->of_offset > inode->i_size) {
                    inode->i_size = file->of_offset;
                }
            }
        }
    }

    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    ssize_t bytes_read = 0;

    if (to_read <= 0) {
        return bytes_read;
    }

    int previously_read_blocks = file->of_offset / BLOCK_SIZE + 1;
    int end_read_blocks = (file->of_offset + to_read) / BLOCK_SIZE + 1;
    size_t current_read_size = BLOCK_SIZE;

    for (int i = previously_read_blocks; i < end_read_blocks; i++, to_read -= BLOCK_SIZE) {
        current_read_size = (to_read > BLOCK_SIZE) ? BLOCK_SIZE : to_read;
        void *block;
        if (i < 10) {
            block = data_block_get(inode->i_data_block[i]);
            if (block == NULL) {
                return -1;
            }
        } else {
            int *indirect_block = (int *) data_block_get(inode->i_indirect_data_block);
            if (indirect_block == NULL) {
                return -1;
            }
            block = data_block_get(indirect_block[i-10]);
            if (block == NULL) {
                return -1;
            }
        }
        memcpy(buffer + file->of_offset, block, current_read_size);
        bytes_read += current_read_size;
        file->of_offset += current_read_size;
    }

    return bytes_read;
}

// TODO -  fazer isto thread-safe, ver https://piazza.com/class/kwp87w2smmq66p?cid=84
int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    int source_handle = tfs_open(source_path, TFS_O_CREAT); // não precisa de flags especificas, so nao pode é ser a TRUNC
    if (source_handle == -1) {
        return -1;
    }

    open_file_entry_t *source_file = get_open_file_entry(source_handle);
    if (source_file == NULL) {
        return -1;
    }

    inode_t *source_inode = inode_get(source_file->of_inumber);
    if (source_inode == NULL) {
        return -1;
    }

    size_t source_size = source_inode->i_size;

    FILE *dest_file = fopen(dest_path, "w");
    if (dest_file == NULL) {
        return -1;
    }

    void *buffer;
    // TODO - abaixo - não tenho a certeza se é BLOCK_SIZE * source_size ou source_size, temos de testar
    memset(buffer, 0, BLOCK_SIZE * source_size);
    size_t bytes_read = tfs_read(source_handle, buffer, source_size * BLOCK_SIZE);
    
    if (bytes_read == -1) {
        return -1;
    }

    // perhaps writing and reading in blocks is better (aka less oportunity of failing)
    // see https://stackoverflow.com/questions/11054750/check-return-value-fread-and-fwrite
    if (fwrite(buffer, sizeof(char), bytes_read, dest_file) != bytes_read) {
        return -1;
    }

    if (tfs_close(source_handle) == -1) {
        return -1;
    }
    fclose(dest_file);

    return 0;
}