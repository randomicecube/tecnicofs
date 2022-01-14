#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define GRN "\x1B[32m"
#define RED "\x1B[31m"
#define RESET "\x1B[0m"
#define NUM_THREADS 4000

void *copy_external(void *arg) {
  // sleep(1);
  char *path = (char *) arg;
  assert(tfs_copy_to_external_fs(path, "test_copy_to_external_fs.out") == 0);
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = "/f1";

  // str to path
  int fd = tfs_open(path, TFS_O_CREAT);
  assert(fd != -1);

  char *str = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.";

  ssize_t bytes_written = tfs_write(fd, str, strlen(str));
  assert(bytes_written == strlen(str));

  pthread_t tid[NUM_THREADS];

  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&tid[i], NULL, copy_external, path) != 0) {
      perror("pthread_create error");
      exit(EXIT_FAILURE);
    }
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_join(tid[i], NULL) != 0) {
      perror("pthread_join error");
      exit(EXIT_FAILURE);
    }
  }

  assert(tfs_close(fd) != -1);

  FILE *fp = fopen("test_copy_to_external_fs.out", "r");
  assert(fp != NULL);
  // check if fp content is equal to str
  char *buf = (char *) malloc(sizeof(char) * strlen(str));
  assert(buf != NULL);
  fread(buf, sizeof(char), strlen(str), fp);
  if (strcmp(buf, str) != 0) {
    printf("%s%s%s", RED, "Test failed\n", RESET);
    printf("buf is %s\n", buf);
    printf("str is %s\n", str);
    exit(EXIT_FAILURE);
  }

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test.\n" RESET);

  exit(EXIT_SUCCESS);
}
