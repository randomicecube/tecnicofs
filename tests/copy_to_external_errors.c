#include "fs/operations.h"
#include <assert.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"

int main() {
    char *path1 = "/f1";

    /* Tests different scenarios where tfs_copy_to_external_fs is expected to fail */

    assert(tfs_init() != -1);
    
    int f1 = tfs_open(path1, TFS_O_CREAT);
    assert(f1 != -1);
    assert(tfs_close(f1) != -1);

    /* Scenario 1: destination file is in directory that does not exist */
    assert (tfs_copy_to_external_fs(path1, "./wrong_dir/unexpectedfile") == -1);

    /* Scenario 2: source file does not exist */
    assert(tfs_copy_to_external_fs("/f2", "out") == -1);

    printf(GRN "Successful test.\n" RESET);

    return 0;
}
