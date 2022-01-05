#include "fs/operations.h"
#include <assert.h>
#include <string.h>

int main() {
    char *path = "/f1";
    char *buffer;
    char *str;

    FILE *lusiadas = fopen("lusiadas.txt", "r");
    long start = ftell(lusiadas);
    fseek(lusiadas, 0, SEEK_END);
    long delta = ftell(lusiadas) - start;
    fseek(lusiadas, start, SEEK_SET);
    buffer = (char *) malloc((size_t)(delta + 1));
    str = (char *) malloc((size_t)(delta + 1));
    fread(buffer, sizeof(char), (size_t) delta, lusiadas);
    fclose(lusiadas);

    assert(tfs_init() != -1);

    int f;
    ssize_t r;

    f = tfs_open(path, TFS_O_CREAT);
    assert(f != -1);

    r = tfs_write(f, buffer, strlen(buffer));
    assert(r == strlen(buffer));

    assert(tfs_close(f) != -1);

    f = tfs_open(path, 0);
    assert(f != -1);

    r = tfs_read(f, str, sizeof(str) - 1);
    assert(r == strlen(buffer));

    buffer[r] = '\0';
    assert(strcmp(buffer, str) == 0);

    assert(tfs_close(f) != -1);

    printf("Successful test.\n");

    free(buffer);
    free(str);

    return 0;
}
