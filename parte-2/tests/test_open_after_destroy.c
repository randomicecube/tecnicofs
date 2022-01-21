#include "fs/operations.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"

/*  Simple test to check whether the implementation of
    tfs_destroy_after_all_closed is correct.
    Tests whether opening a file after a call to
    tfs_destroy_after_all_closed and before tfs_init returns -1.
    Note: This test uses TecnicoFS as a library, not
    as a standalone server.
*/

int closed_file = 0;
int f;

void *fn_thread(void *arg) {
    (void)
        arg; /* Since arg is not used, this line prevents a compiler warning */

    sleep(1);

    /* set *before* closing the file, so that it is set before
       tfs_destroy_after_all_close returns in the main thread
    */
    closed_file = 1;

    assert(tfs_close(f) != -1);

    return NULL;
}

int main() {
    
    assert(tfs_init() != -1);

    pthread_t t;
    f = tfs_open("/f1", TFS_O_CREAT);
		assert(f != -1);

    assert(pthread_create(&t, NULL, fn_thread, NULL) == 0);
		assert(tfs_destroy_after_all_closed() != -1);
    assert(closed_file == 1);

    // new additions below
    assert(pthread_join(t, NULL) == 0);

    // tests whether opening a file before init returns -1
    assert(tfs_open("/f1", TFS_O_CREAT) == -1);

    assert(tfs_init() != -1);

    // tests if it now opens as expected (after init)
    assert((f = tfs_open("/f1", TFS_O_CREAT)) != -1);

    assert(tfs_close(f) != -1);

    assert(tfs_destroy_after_all_closed() != -1);

    printf(GRN "Successful test.\n" RESET);

    return 0;
}
