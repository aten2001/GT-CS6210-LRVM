#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define TEST_STRING "hello, world"
#define OFFSET2 100000

/* proc1 writes some data, commits it, then exits */
void proc1() 
{
    rvm_t rvm;
    trans_t trans;
    int i;
    char* segs[1];
    rvm = rvm_init("rvm_segments");
    rvm_destroy(rvm, "testseg");
    segs[0] = (char*) rvm_map(rvm, "testseg", 100000);
    for(i=0; i<100000; i++){
        trans = rvm_begin_trans(rvm, 1, (void**) segs);
        rvm_about_to_modify(trans, segs[0], i, 1);
        sprintf(segs[0], "%c", i%200+30);
        rvm_commit_trans(trans);
    }
    abort();
}


/* proc2 opens the segments and reads from them */
void proc2() 
{
    char* segs[1];
    int i;
    rvm_t rvm;
    rvm = rvm_init("rvm_segments");
    segs[0] = (char *) rvm_map(rvm, "testseg", 100000);
    for(i=0; i<100000; i++){
        if(segs[0][i] != i%200+30) {
            printf("ERROR: mem[%d]: %d != %d\n", i, segs[0][i], i%200+30);
            exit(2);
        }
    }
    printf("OK\n");
    exit(0);
}


int main(int argc, char **argv)
{
    int pid;

    pid = fork();
    if(pid < 0) {
        perror("fork");
        exit(2);
    }
    if(pid == 0) {
        proc1();
        exit(0);
    }

    waitpid(pid, NULL, 0);

    proc2();

    return 0;
}
