#include "../fs/operations.h"
#include "../fs/state.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"

int main(){
    
    char paths[20][5];
    char strs[20][11];
    char path[5];
    char id[3];
    char description[12];
    //int files[20];

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


    assert(tfs_init() != -1);

    for (int k = 0; k < 20; k++){
        int file = tfs_open(paths[k], TFS_O_CREAT);
        assert(file != -1);

        assert(tfs_write(file, strs[k], strlen(strs[k])) != -1);

        assert(tfs_close(file) != -1);
        //files[k] = k;
    }

    printf(GRN "Successful test.\n" RESET);

    return 0;
}