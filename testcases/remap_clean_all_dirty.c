/* Involve a segment in multiple transactions, each transaction modifying a
 * region and either committing or aborting.  Check the final state. */

#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
     rvm_t rvm = rvm_init("rvm_segments");
     rvm_destroy(rvm, "testseg1");
     rvm_destroy(rvm, "testseg2");
     char* segs[2];
     segs[0] = (char *) rvm_map(rvm, "testseg1", 6);
     segs[1] = (char *) rvm_map(rvm, "testseg2", 6);
     
     trans_t tid;

     /* zero out */
     tid = rvm_begin_trans(rvm, 2, (void **) segs);
     rvm_about_to_modify(tid, segs[0], 0, 2);
     sprintf(segs[0], "a");
     rvm_commit_trans(tid);

     tid = rvm_begin_trans(rvm, 2, (void **) segs);
     rvm_about_to_modify(tid, segs[1], 0, 2);
     sprintf(segs[1], "x");
     rvm_commit_trans(tid);

     rvm_unmap(rvm, segs[0]);
     rvm_unmap(rvm, segs[1]);

     segs[0] = (char *) rvm_map(rvm, "testseg1", 10);
     tid = rvm_begin_trans(rvm, 2, (void **) segs);
     rvm_about_to_modify(tid, segs[0], 1, 2);
     sprintf(&(segs[0][1]), "b");
     rvm_commit_trans(tid);

     segs[1] = (char *) rvm_map(rvm, "testseg2", 10);
     tid = rvm_begin_trans(rvm, 2, (void **) segs);
     rvm_about_to_modify(tid, segs[1], 1, 2);
     sprintf(&(segs[1][1]), "y");
     rvm_commit_trans(tid);
     assert(strcmp("ab", segs[0]) == 0 && strcmp("xy", segs[1]) == 0);

     printf("OK\n");

     return 0;
}
