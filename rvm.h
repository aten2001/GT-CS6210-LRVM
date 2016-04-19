#ifndef __LIBRVM__
#define __LIBRVM__

#include "rvm_internal.h"
#include "steque.h"
#include "seqsrchst.h"

#include <stdio.h>

#define trans_t int
#define rvm_t RecoverableVM*

struct log_t{
    int segID;
    int offset;
    int length;
    void *mem;
};

struct RVM_transaction{
    trans_t id;
    int numsegs;
    void **segbases;
    steque_t log;
};

struct RecoverableVM{
    char *directory; //directory name

    trans_t transID; // last transaction ID
    steque_t transaction; // transaction stack
    seqsrchst_t segnameMap; // (key, value) = (segbase, segname) map
    seqsrchst_t segbaseMap; // (key, value) = (segname, segbase) map
    
    unsigned long int log_id;
    unsigned long int log_id_min;
    FILE *log_file;
};

/* Formal API */
rvm_t rvm_init(const char *directory);
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create);
void rvm_unmap(rvm_t rvm, void *segbase);
void rvm_destroy(rvm_t rvm, const char *segname);
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases);
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size);
void rvm_commit_trans(trans_t tid);
void rvm_abort_trans(trans_t tid);
void rvm_truncate_log(rvm_t rvm);

////////////////////////////////////////////////////////////////////
/* Utility function */
void rvm_destructor(rvm_t rvm);


RVM_transaction *getTransaction(trans_t tid);
void freeTransaction(RVM_transaction* transaction);
void commitTransaction(RVM_transaction* transaction, const int index, const int max, FILE* logFile);
void truncateLog(rvm_t rvm, unsigned long int id);

const void *getSegbase(const char* segname);
const char *getSegname(void* segbase);
void removeMapping(void* segbase);
#endif
