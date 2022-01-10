#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define NUM_THREADS 274 // random number

// struct that keeps the current iteration and file descriptor
typedef struct {
  char *path;
  pthread_mutex_t lock;
} thread_data;

void *write_thread(void *arg) {
  thread_data *data = (thread_data *) arg;
  char *buffer = "Lorem ipsum, or lipsum as it is sometimes known, is dummy text used in laying out print, graphic or web designs. The passage is attributed to an unknown typesetter in the 15th century who is thought to have scrambled parts of Cicero's De Finibus Bonorum et Malorum for use in a type specimen book.";
  lock_mutex(&data->lock);
  int fd = tfs_open(data->path, TFS_O_TRUNC);
  assert(fd != -1);
  unlock_mutex(&data->lock);
  ssize_t r = tfs_write(fd, buffer, strlen(buffer));
  assert(tfs_close(fd) != -1);
  assert(r == strlen(buffer));
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = "/f1";
  int fd = tfs_open(path, TFS_O_CREAT);
  assert(fd != -1);
  assert(tfs_close(fd) != -1);

  thread_data *data = malloc(sizeof(thread_data));
  if (data == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }
  data->path = path;
  init_mutex(&data->lock);
  
  pthread_t tid[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&tid[i], NULL, write_thread, data) != 0) {
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

  destroy_mutex(&data->lock);

  free(data);

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test\n" RESET);

  exit(EXIT_SUCCESS);
}