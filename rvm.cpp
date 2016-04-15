#include"rvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

RecoverableVM* rvm = NULL;

/*
   Initialize the library with the specified directory as backing store.
   */
rvm_t rvm_init(const char *directory){
    if(rvm != NULL){
        //TODO
    }

    rvm = (RecoverableVM*) malloc (sizeof(RecoverableVM));
    rvm->directory = (char*) malloc (sizeof(char) * strlen(directory));
    strncpy(rvm->directory, directory, strlen(directory));

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

    // Create Log file
    char log_path[strlen(directory) + 20];
    sprintf(log_path, "%s/.log%lu", directory, rvm->log_id);
    rvm->log_file = fopen(log_path, "wb+");

    // Initial transaction metadata
    rvm->transID = 0;

    // Update logID
    logID_file = fopen(logId_path, "w");
    fprintf(logID_file, "%lu", ++rvm->log_id);
    fclose(logID_file);

    return rvm;
}

/*
   map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice.
   */
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
    // If segname is already mapped, return immediate
    for(std::vector<char*>::iterator it = rvm->segname.begin(); it != rvm->segname.end(); it++){
        if (!strcmp(*it, segname)) {
            fprintf(stderr, "map %s with size %d failed\n", segname, size_to_create);
            return (void*) -1;
        }
    }

    char *file_name = (char*) malloc (sizeof(char) * (strlen(rvm->directory) + strlen(segname) + 1));
    sprintf(file_name, "%s/%s", rvm->directory, segname);

    FILE *target = fopen(file_name, "wb");
    char *mem = (char*) malloc (sizeof(char) * size_to_create);
    fread(mem, 1, size_to_create, target);

    char *name = (char*) malloc (sizeof(char) * strlen(segname));
    strncpy(name, segname, strlen(segname));
    rvm->segname.push_back(name);
    rvm->segmem.push_back(mem);
    return mem;
}

/*
   unmap a segment from memory.
   */
void rvm_unmap(rvm_t rvm, void *segbase){
    int index = -1;
    for(unsigned int i=0; i<rvm->segmem.size(); i++){
        if(rvm->segmem[i] == segbase) {
            index = i;
            break;
        }
    }

    if(index >= 0){
        void *name = rvm->segname[index];
        void *mem = rvm->segmem[index];
        rvm->segmem.erase(rvm->segmem.begin() + index);
        rvm->segname.erase(rvm->segname.begin() + index);
        free(name);
        free(mem);
    } else {
        fprintf(stderr, "unmap %p failed\n", segbase);
    }
}

/*
   destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped.
   */
void rvm_destroy(rvm_t rvm, const char *segname){
    // if segname is current mapped, return immediately
    for(std::vector<char*>::iterator it = rvm->segname.begin(); it != rvm->segname.end(); it++){
        if (!strcmp(*it, segname)) {
            fprintf(stderr, "destory segname %s failed\n", segname);
            return;
        }
    }

    char filename[strlen(rvm->directory) + strlen(segname) + 1];
    sprintf(filename, "%s/%s", rvm->directory, segname);
    unlink(filename);
}

/*
   begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trans_t needs to be able to be typecasted to an integer type.
   */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases){
    // TODO If any of the specified segments is already being modified by a transaction, the call should fail and return (trans_t) -1.
    
    RVM_transaction *transaction = (RVM_transaction*) malloc (sizeof(RVM_transaction));
    transaction->id = rvm->transID++;
    transaction->numsegs = numsegs;
    transaction->segbases = segbases;
    for(int i=0; i<numsegs; i++){
        transaction->offset.push_back(std::vector<int>());
        transaction->length.push_back(std::vector<int>());
        transaction->undo.push_back(std::vector<char*>());
    }
    rvm->transaction.push_back(transaction);
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
                transaction->offset[i].push_back(offset);
                transaction->length[i].push_back(size);

                char *backup = (char*) malloc (sizeof(char) * size);
                memcpy(backup, (char*) transaction->segbases[i] + offset, size);
                transaction->undo[i].push_back(backup);
                transaction->numlog++;
                return;
            }
        }
    }
}

/*
   commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts.
   */
void rvm_commit_trans(trans_t tid){
    RVM_transaction *transaction = getTransaction(tid);
    if(transaction != NULL){
        FILE *log = rvm->log_file;
        fwrite("TBegin", 1, 2, log);
        fwrite(&transaction->numlog, sizeof(int), 1, log);
        
        for(int i=0; i<transaction->numsegs; i++){
            const char *segname = getSegname(transaction->segbases[i]);
            for(unsigned int j=0; j<transaction->offset[i].size(); j++){
                printf("\tcommit\t[%s]\toffset: %d, size: %d\n", segname, transaction->offset[i][j], transaction->length[i][j]);
                fwrite(segname, sizeof(char), strlen(segname), log);
                fwrite(&transaction->offset[i][j], sizeof(int), 1, log);
                fwrite(&transaction->length[i][j], sizeof(int), 1, log);
                fwrite((char*)transaction->segbases[i] + transaction->offset[i][j], sizeof(char), transaction->length[i][j], log);
            }
        }
        fwrite("TEnd", 1, 2, log);
        freeTransaction(transaction);
    }
}

/*
   undo all changes that have happened within the specified transaction.
   */
void rvm_abort_trans(trans_t tid){
    RVM_transaction *transaction = getTransaction(tid);
    if(transaction != NULL){
        for(int i=0; i<transaction->numsegs; i++){
            for(int j=transaction->offset[i].size()-1; j>=0; j--){
                printf("\tundo\t[%s]\toffset: %d, size: %d\n", getSegname(transaction->segbases[i]), transaction->offset[i][j], transaction->length[i][j]);
                memcpy((char*) transaction->segbases[i]+transaction->offset[i][j], transaction->undo[i][j], transaction->length[i][j]);
            }
        }
        freeTransaction(transaction);
    }
}

/*
   play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible.
   */
void rvm_truncate_log(rvm_t rvm){


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void freeTransaction(RVM_transaction* transaction){
    if(transaction == NULL) return;

    for(int i=0; i<transaction->numsegs; i++){
        for(int j=0; j<transaction->undo[i].size(); j++){
            free(transaction->undo[i][j]);
        }
    }


    for(std::vector<RVM_transaction*>::iterator it = rvm->transaction.begin(); it != rvm->transaction.end(); it++){
        if((*it)->id == transaction->id){
            rvm->transaction.erase(it);
            break;
        }
    }

    printf("trans[%d] finished\n", transaction->id);


}

RVM_transaction *getTransaction(trans_t tid){
    RVM_transaction *transaction = NULL;
    for(std::vector<RVM_transaction*>::iterator it = rvm->transaction.begin(); it != rvm->transaction.end(); it++){
        if((*it)->id == tid){
            transaction = *it;
            break;
        }
    }
    return transaction;
}

const char *getSegname(void *segbase){
    int index = -1;
    for(unsigned int i=0; i<rvm->segmem.size(); i++){
        if(rvm->segmem[i] == segbase) {
            index = i;
            break;
        }
    }

    if(index == -1) return NULL;
    else return rvm->segname[index];
}
