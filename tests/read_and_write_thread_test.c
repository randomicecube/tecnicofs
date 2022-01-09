#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"

// struct that keeps the current iteration and file descriptor
typedef struct {
    int iteration;
    int fd;
    pthread_mutex_t lock;
} thread_data;

void *write_thread(void *arg) {
  // arg is the thread_data struct
    thread_data *data = (thread_data *) arg;
  // write current iteration in the buffer and write the buffer, with tfs_write, in the file with fd
    char buffer[40];

    char paths[20][5];
    char strs[20][11];
    char path[5];
    char id[3];
    char description[12];

    ssize_t r[20];

    for (int i = 1; i < 21; i++){
        sprintf(id, "%d", i);
        strcpy(path, "/f");
        strcat(path, id);
        strcpy(paths[i-1], path);
    }

    for (int j = 1; j < 21; j++){
        sprintf(id, "%d", j);
        strcpy(description, "OLA MUNDO");
        strcat(description, id);
        strcpy(strs[j-1], description);
    }

    for (int m = 0; m < 20; m++){
        int fd = tfs_open(paths[m], TFS_O_CREAT);
        r[m] = tfs_write(fd, strs[m], strlen(strs[m]));
        assert(r[m] == strlen(strs[m]));
        assert(tfs_close(fd) != -1); //??? nao sei se isto está correto aqui
    }

    return NULL;
}

void *read_thread(void *arg) {
    thread_data *data = (thread_data *) arg;
    ssize_t r[20];

    //falta aqui ir buscar os ficheiros certos, nao sei se preferes fazer com
    //pointers ou global
    
    for(int n = 0; n < 20; n++){
        char *buffer = malloc(BLOCK_SIZE);
        r[m] = tfs_read(data->fd, buffer, BLOCK_SIZE);
        free(buffer);
    }
    return NULL;
}


int main(){
  
    assert(tfs_init() != -1);



    //escrita e leitura para 2 ficheiros

    thread_data *data = malloc(sizeof(thread_data));
    if (data == NULL){
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_init(&data->lock, NULL)){
      perror("mutex init error");
      exit(EXIT_FAILURE);
    }

    pthread tid[6*BLOCK_SIZE];
    for (int i = 0; i < 6*BLOCK_SIZE; i++){
        lock_mutex(&data->lock);
        data->iteration = i;
        unlock_mutex(&data->lock);
        pthread_create(&tid[i], NULL, write_thread, data);
        pthread_create(&tid[i], NULL, read_thread, data);
    }

    for (int i = 0; i < 6*BLOCK_SIZE; i++){
        pthread_join(tid[i], NULL);
    }

    if (pthread_mutex_destroy(&data->lock)){
        perror("mutex destroy error");
        exit(EXIT_FAILURE);
    }

    assert(tfs_destroy() != -1);

    printf(GRN "Successful test\n" RESET);

    exit(EXIT_SUCCESS);
}