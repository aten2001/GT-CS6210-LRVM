#ifndef __LIBRVM__
#define __LIBRVM__

#include "rvm_internal.h"

#include <stdio.h>
#include <vector>

#define trans_t int
#define rvm_t RecoverableVM*
struct RVM_transaction{
    trans_t id;

    int numsegs;
    void **segbases;
    std::vector<std::vector<int> > offset;
    std::vector<std::vector<int> > length;
    std::vector<std::vector<char*> > undo;

    int numlog;
};


struct RecoverableVM{
    char *directory;
    std::vector<char*> segname;
    std::vector<void*> segmem;

    trans_t transID;
    std::vector<RVM_transaction*> transaction;
    
    unsigned long int log_id;
    unsigned long int log_id_min;
    FILE *log_file;
};


rvm_t rvm_init(const char *directory);
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);
void rvm_unmap(rvm_t rvm, void *segbase);
void rvm_destroy(rvm_t rvm, const char *segname);
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);
void rvm_commit_trans(trans_t tid);
void rvm_abort_trans(trans_t tid);
void rvm_truncate_log(rvm_t rvm);


void freeTransaction(RVM_transaction* transaction);
RVM_transaction *getTransaction(trans_t tid);
const char *getSegname(void* segbase);
void truncateLog(rvm_t rvm, unsigned long int id);
#endif
