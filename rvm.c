#include"rvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

RecoverableVM* rvm = NULL;

/* Initialize the library with the specified directory as backing store. */
int nameCMP(seqsrchst_key a, seqsrchst_key b){
    return !strcmp((char*) a, (char*) b);
}

int baseCMP(seqsrchst_key a, seqsrchst_key b){
    return a == b;
}

rvm_t rvm_init(const char *directory){
    if(rvm != NULL){
        fprintf(stderr, "Multiple RVM instances detected\n");
        return NULL;
    }

    rvm = (RecoverableVM*) malloc (sizeof(RecoverableVM));
    rvm->directory = (char*) malloc (sizeof(char) * (strlen(directory)+1));
    strncpy(rvm->directory, directory, strlen(directory)+1);

    // Create Directory
    struct stat st;
    if(stat(directory, &st) != 0 && mkdir(directory, 0777) != 0){
        fprintf(stderr, "Create Directory Failed\n");
        perror("rvm_init:mkdir");
        return NULL;
    }

    // Read Current Log ID
    char logId_path[strlen(directory) + 20];
    sprintf(logId_path, "%s/.rvm_logID", directory);
    FILE *logID_file = fopen(logId_path, "r");
    rvm->log_id = 0;
    if(logID_file != NULL){
        fscanf(logID_file, "%lu", &rvm->log_id);
        fclose(logID_file);
    }

    // Find minimum log id. It will be used to truncate log.
    rvm->log_id_min = rvm->log_id;
    struct dirent *dp;
    DIR *dir = opendir(directory);
    unsigned long int n;
    while ((dp=readdir(dir)) != NULL) {
        if(strncmp(dp->d_name, ".log", 2) == 0) {
            n = atoi(&(dp->d_name[4]));
            if(n < rvm->log_id_min) {
                rvm->log_id_min = n;
            }
        }
    }
    closedir(dir);

    rvm_truncate_log(rvm);
    rvm->log_id = 0;

    // Update logID
    logID_file = fopen(logId_path, "w");
    fprintf(logID_file, "%lu", rvm->log_id + 1);
    fclose(logID_file);

    // Create Log file
    char log_path[strlen(directory) + 20];
    sprintf(log_path, "%s/.log%lu", directory, rvm->log_id);
    rvm->log_file = fopen(log_path, "wb+");

    // Initial Internal data structure
    rvm->transID = 0;
    seqsrchst_init(&rvm->segnameMap, baseCMP);
    seqsrchst_init(&rvm->segbaseMap, nameCMP);
    return rvm;
}

/*
   map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice.
   */
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
    // If segname is already mapped, return immediate
    if(getSegbase(segname) != NULL){
        fprintf(stderr, "map %s with size %d failed\n", segname, size_to_create);
        return (void*) -1;
    }

    // Open file and truncate to size_to_create
    char file_name[strlen(rvm->directory) + strlen(segname) + 1];
    sprintf(file_name, "%s/%s", rvm->directory, segname);

    int fd = open(file_name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    struct stat sb;
    fstat(fd, &sb);
    if((unsigned long) sb.st_size < (size_to_create * sizeof(char))) {
        ftruncate(fd, (size_to_create * sizeof(char)));
    }

    // Read data from disk, load to memory
    char *mem = (char*) malloc (sizeof(char) * size_to_create);
    read(fd, (void*)mem, sizeof(char) * size_to_create);
    close(fd);
    
    // Create internal data structure mapping
    char *name = (char*) malloc (sizeof(char) * strlen(segname) + 1);
    strncpy(name, segname, strlen(segname)+1);
    seqsrchst_put(&rvm->segnameMap, mem, name);
    seqsrchst_put(&rvm->segbaseMap, name, mem);
    return mem;
}

/* unmap a segment from memory. */
void rvm_unmap(rvm_t rvm, void *segbase){
    // if segbase is not currently mapped, return immediately.
    if(getSegname(segbase) == NULL){
        fprintf(stderr, "unmap %p failed\n", segbase);
        return;
    }

    // delete internal data structure mapping
    removeMapping(segbase);
}

/* destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped. */
void rvm_destroy(rvm_t rvm, const char *segname){
    // if segname is current mapped, return immediately
    if(getSegbase(segname) != NULL){
        fprintf(stderr, "destory segname %s failed\n", segname);
        return;
    }

    // delete disk hard-copy
    char filename[strlen(rvm->directory) + strlen(segname) + 1];
    sprintf(filename, "%s/%s", rvm->directory, segname);
    unlink(filename);
}

/*
   begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trans_t needs to be able to be typecasted to an integer type.
   */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases){
    // If any segbases is modified by other transaction, return immediately
    for(int i=0; i<numsegs; i++){
        int size = steque_size(&rvm->transaction);
        for(int j=0; j<size; j++){
            RVM_transaction* transaction = (RVM_transaction*) steque_pop(&rvm->transaction);
            steque_push(&rvm->transaction, transaction);

            for(int k=0; k<transaction->numsegs; k++){
                if(segbases[i] == transaction->segbases[k]){
                    fprintf(stderr, "begin trans %p failed\n", segbases[i]);
                    return (trans_t) -1;
                }
            }
        }
    }

    RVM_transaction *transaction = (RVM_transaction*) malloc (sizeof(RVM_transaction));
    transaction->id = rvm->transID++;
    transaction->numsegs = numsegs;
    transaction->segbases = segbases;

    steque_push(&rvm->transaction, transaction);
    printf("trans[%d] begin\n", transaction->id);
    return transaction->id;
}

/*
   declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in the call to rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal call rvm_about_to_modify multiple times on the same memory area.
   */
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size){
    RVM_transaction *transaction = getTransaction(tid);

    if(transaction != NULL){
        for(int i=0; i<transaction->numsegs; i++){
            if(transaction->segbases[i] == segbase){
                printf("\tmodify\t[%s]\toffset: %d, size: %d\n", getSegname(segbase), offset, size);
                log_t *record = (log_t*) malloc (sizeof(log_t));
                record->segID = i;
                record->offset = offset;
                record->length = size;
                record->mem = (char*) malloc (sizeof(char) * size);
                memcpy(record->mem, (char*) transaction->segbases[i] + offset, size);

                steque_push(&transaction->log, record);
                return;
            }
        }
    }
}

/* commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts. */

void rvm_commit_trans(trans_t tid){
    RVM_transaction *transaction = getTransaction(tid);
    if(transaction != NULL){
        FILE *log = rvm->log_file;
        int size = steque_size(&transaction->log);
        fwrite("TB", 1, 2, log);
        fwrite(&size, sizeof(int), 1, log);
        commitTransaction(transaction, 0, size, log);
        fwrite("TE", 1, 2, log);
        fflush(log);
        freeTransaction(transaction);
    }
}

/* undo all changes that have happened within the specified transaction. */
void rvm_abort_trans(trans_t tid){
    RVM_transaction *transaction = getTransaction(tid);
    if(transaction != NULL){
        int size = steque_size(&transaction->log);
        for(int i=0; i<size; i++){
            // Revert back and free malloc memory
            log_t *log = (log_t*) steque_pop (&transaction->log);
            void *segbase = transaction->segbases[log->segID];
            int offset = log->offset;
            int length = log->length;
            memcpy((char*) segbase + offset, log->mem, length);
            printf("\tundo\t[%s]\toffset: %d, size: %d\n", getSegname(segbase), offset, length);

            free(log->mem);
            free(log);
        }
        freeTransaction(transaction);
    }
}

/*
   play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible.
   */
void rvm_truncate_log(rvm_t rvm){
    unsigned long int id;
    for (id = rvm->log_id_min; id < rvm->log_id; id++) {
        printf("truncate .log%lu\n", id);
        truncateLog(rvm, id);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Utility functions */
void rvm_destructor(rvm_t rvm){
    //abort remaining transaction
    int size = steque_size(&rvm->transaction);
    for(int i=0; i<size; i++){
        RVM_transaction *transaction = (RVM_transaction*) steque_front (&rvm->transaction);
        rvm_commit_trans(transaction->id);
    }

    //unmap remaining segbase, segname
    while(!seqsrchst_isempty(&rvm->segnameMap)){
        void *segbase = rvm->segnameMap.first->key;
        removeMapping(segbase);
    }

    //free additional memory
    free(rvm->directory);
    fclose(rvm->log_file);
    free(rvm);
}



int checkTransaction(rvm_t rvm, char *cur, char *end) {
    if (cur >= end) return -1;
    return 0;
}

char* redoTransaction(rvm_t rvm, char *cur, char *end) {
    char seg_path[strlen(rvm->directory) + 20];
    int fd = -1;
    struct stat sb;

    char *previousSegname = NULL;
    char *segname;
    char *segbase = NULL;

    int numlog, offset, length;
    if(checkTransaction(rvm, cur, end) < 0) return NULL;
    cur += 2;
    numlog = *((int*)cur);
    cur += sizeof(int);
    while(numlog) {
        segname = cur;
        cur += strlen(segname) + 1;
        offset = *((int*)cur);
        cur += sizeof(int);
        length = *((int*)cur);
        cur += sizeof(int);
        printf("%s %d %d\n", segname, offset, length);
        if (!previousSegname || strcmp(segname, previousSegname) != 0) {
            if (segbase) {
                printf("unmap %s\n", previousSegname);
                munmap(segbase, sb.st_size);
                close(fd);
            }
            printf("mmap %s\n", segname);
            sprintf(seg_path, "%s/%s", rvm->directory, segname);
            fd = open(seg_path, O_RDWR);
            if(fd == -1 || fstat(fd, &sb) == -1){
                perror("ERROR:");
                fprintf(stderr, "truncate %s failed\n", seg_path);
                return NULL;
            }
            segbase = (char*)mmap(NULL, sb.st_size, PROT_WRITE, MAP_SHARED, fd, 0);
            previousSegname = segname;
        }
        memcpy(segbase + offset, cur, length);
        cur += length;
        numlog--;
    }

    if (segbase) {
        printf("unmap %s\n", previousSegname);
        munmap(segbase, sb.st_size);
        close(fd);
    }

    cur += 2;
    return cur;    
}

void truncateLog(rvm_t rvm, unsigned long int id) {
    char log_path[strlen(rvm->directory) + 20];
    int fd;
    struct stat sb;
    char *addr;
    char *cur;
    char *end;

    sprintf(log_path, "%s/.log%lu", rvm->directory, id);
    fd = open(log_path, O_RDONLY);
    if (fd == -1 || fstat(fd, &sb) == -1){
        perror("ERROR:");
        fprintf(stderr, "truncate %s failed\n", log_path);
        return;
    }

    cur = addr = (char*)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    end = addr + sb.st_size;
    while(cur) {
        cur = redoTransaction(rvm, cur, end);
    }
    munmap(addr, sb.st_size);
    close(fd);
    unlink(log_path);
}

void freeTransaction(RVM_transaction* transaction){
    if(transaction == NULL) return;

    // Free remaining log record
    int size = steque_size(&transaction->log);
    for(int i=0; i<size; i++){
        log_t *record = (log_t*) steque_pop (&transaction->log);
        free(record->mem);
        free(record);
    }

    // Remove transaction from rvm->transcation list
    size = steque_size(&rvm->transaction);
    for(int i=0; i<size; i++){
        RVM_transaction *first = (RVM_transaction*) steque_pop (&rvm->transaction);
        if(first->id == transaction->id)
            break;
        else
            steque_push(&rvm->transaction, first);
    }

    free(transaction);
    printf("trans[%d] finished\n", transaction->id);
}

void commitTransaction(RVM_transaction *transaction, const int cur, const int max, FILE* log){
    if(transaction == NULL) return;
    if(cur >= max || cur < 0) return;

    log_t *record = (log_t*) steque_pop (&transaction->log);
    if(record == NULL) return;
    commitTransaction(transaction, cur+1, max, log);
    
    void *mem = transaction->segbases[record->segID];
    int offset = record->offset;
    int length = record->length;

    const char* segname = getSegname(mem);
    printf("\tcommit\t[%s]\toffset: %d, size: %d\n", segname, offset, length);
    fwrite(segname, sizeof(char), strlen(segname) + 1, log);
    fwrite(&offset, sizeof(int), 1, log);
    fwrite(&length, sizeof(int), 1, log);
    fwrite((char*) mem + offset, sizeof(char), length, log);
    
    free(record->mem);
    free(record);
}

RVM_transaction *getTransaction(trans_t tid){
    RVM_transaction *transaction = NULL;

    int size = steque_size(&rvm->transaction);
    for(int i=0; i<size; i++){
        transaction = (RVM_transaction*) steque_pop(&rvm->transaction);
        steque_push(&rvm->transaction, transaction);
        if(transaction->id == transaction->id)
            break;
    }
    return transaction;
}

const void *getSegbase(const char *segname){
    return (char*) seqsrchst_get(&rvm->segbaseMap, (void*)segname);
}

const char *getSegname(void *segbase){
    return (char*) seqsrchst_get(&rvm->segnameMap, segbase);
}

void removeMapping(void *segbase){
    char *segname = (char*) getSegname(segbase);
    if(segname == NULL) return;

    seqsrchst_delete(&rvm->segnameMap, segbase);
    seqsrchst_delete(&rvm->segbaseMap, segname);
    free(segbase);
    free(segname);
}