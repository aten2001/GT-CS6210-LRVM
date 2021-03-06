#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/wait.h>


void proc1() 
{
     rvm_t rvm = rvm_init("rvm_segments");
     rvm_destroy(rvm, "testseg1");
     rvm_destroy(rvm, "testseg2");
     char* segs[2];
     segs[0] = (char *) rvm_map(rvm, "testseg1", 6);
     segs[1] = (char *) rvm_map(rvm, "testseg2", 6);

     trans_t tid;

     tid = rvm_begin_trans(rvm, 2, (void **) segs);
     rvm_about_to_modify(tid, segs[0], 0, 5);
     sprintf(segs[0], "    ");
     rvm_commit_trans(tid);

     rvm_unmap(rvm, segs[1]);

     segs[1] = (char *) rvm_map(rvm, "testseg2", 10);
     tid = rvm_begin_trans(rvm, 2, (void **) segs);
     rvm_about_to_modify(tid, segs[1], 0, 2);
     sprintf(&(segs[1][0]), "a");
     rvm_about_to_modify(tid, segs[1], 1, 2);
     sprintf(&(segs[1][1]), "z");
     rvm_commit_trans(tid);

     abort();
}

void proc2() 
{
     char* segs[2];
     rvm_t rvm;

     rvm = rvm_init("rvm_segments");

     segs[0] = (char *) rvm_map(rvm, "testseg1", 10000);
     assert(strcmp("    ", segs[0]) == 0);

     segs[1] = (char *) rvm_map(rvm, "testseg2", 10000);
     assert(strcmp("az", segs[1]) == 0);

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
