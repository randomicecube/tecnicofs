#include "fs/operations.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"

int main() {
    char *path = "/f1";
    char *buffer;
    char *str;

    assert(tfs_init() != -1);

    FILE *fp = fopen("lusiadas.txt", "r");
    size_t start = (size_t) ftell(fp);
    fseek(fp, 0, SEEK_END);
    size_t end = (size_t) ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size_t delta = end - start;
    str = malloc(delta + 1);
    if (str == NULL) {
        perror("malloc error");
        exit(EXIT_FAILURE);
    }
    if (fread(str, 1, delta, fp) != delta) {
        return -1;
    }
    str[delta] = '\0';
    fclose(fp);

    buffer = malloc(delta + 1);
    if (buffer == NULL) {
        perror("malloc error");
        exit(EXIT_FAILURE);
    }
    memset(buffer, 0, delta + 1);
    
    int fd = tfs_open(path, TFS_O_CREAT);
    assert(fd != -1);

    ssize_t bytes_written = tfs_write(fd, str, strlen(str));
    assert(bytes_written == strlen(str));

    assert(tfs_close(fd) != -1);

    fd = tfs_open(path, 0);
    assert(fd != -1);

    ssize_t bytes_read = tfs_read(fd, buffer, delta);
    assert(bytes_read == delta);
    buffer[delta] = '\0';

    assert(strcmp(buffer, str) == 0);
    assert(tfs_close(fd) != -1);

    printf(GRN "Successful test.\n" RESET);

    free(str);
    free(buffer);

    return 0;
}
