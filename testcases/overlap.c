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
     char* segs[1];
     segs[0] = (char *) rvm_map(rvm, "testseg1", 6);

     trans_t tid;

     tid = rvm_begin_trans(rvm, 1, (void **) segs);
     rvm_about_to_modify(tid, segs[0], 0, 6);
     sprintf(segs[0], "aaaaa");
     rvm_about_to_modify(tid, segs[0], 3, 6);
     sprintf(&segs[0][3], "bbbbb");
     rvm_abort_trans(tid);
     assert(strlen(segs[0]) == 0);


     tid = rvm_begin_trans(rvm, 1, (void **) segs);
     rvm_about_to_modify(tid, segs[0], 0, 6);
     sprintf(segs[0], "ccccc");
     rvm_about_to_modify(tid, segs[0], 3, 6);
     sprintf(&segs[0][3], "ddddd");
     rvm_commit_trans(tid);

     abort();
}

void proc2() 
{
     char* segs[1];
     rvm_t rvm;

     rvm = rvm_init("rvm_segments");

     segs[0] = (char *) rvm_map(rvm, "testseg1", 10000);
     assert(strcmp("cccddddd", segs[0]) == 0);

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
