#include <kernel/types.h>
#include <user/user.h>
#include "stddef.h"

int main(int argc, char* argv[]){
    int ptoc[2], ctop[2]; // parent_to_child, child_to_parent
    char buf[2];
    pipe(ptoc);
    pipe(ctop);
    if (fork() == 0){
        read(ptoc[0], buf, 1);
        printf("%d: received ping\n", getpid());
        write(ctop[1], "c", 1);
    }
    else{
        write(ptoc[1], "p", 1);
        wait(NULL);
        read(ctop[0], buf, 1);
        printf("%d: received pong\n", getpid());
    }
    exit(0);
}