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

trans_t RVM_transaction::max_id = 0;
RecoverableVM* rvm = NULL;

/* Initialize the library with the specified directory as backing store. */
rvm_t rvm_init(const char *directory){
    if(rvm != NULL){
        fprintf(stderr, "Multiple RVM instances detected\n");
        return NULL;
    }

    rvm = new RecoverableVM();
    rvm->directory = std::string(directory);

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
    resetLog(rvm);
    
    return rvm;
}

/* map a segment from disk into memory. If the segment does not already exist, then create it and give it size size_to_create. If the segment exists but is shorter than size_to_create, then extend it until it is long enough. It is an error to try to map the same segment twice. */
void *rvm_map(rvm_t rvm, const char *segname, int size_to_create){
    // If segname is already mapped, return immediate
    if(rvm->getBase(segname) != NULL){
        fprintf(stderr, "map %s with size %d failed\n", segname, size_to_create);
        return (void*) -1;
    }

    printf("********rvm_map[%s]\tDirty: %d********\n", segname, isDirty(rvm, segname));
    if(isDirty(rvm, segname)) {
        if(rvm->log_file) {
            fclose(rvm->log_file);
            rvm->log_id++;
        }
        rvm_truncate_log(rvm);
        resetLog(rvm);

        rvm->dirtyMap.erase(rvm->dirtyMap.begin(), rvm->dirtyMap.end());
    }

    // Open file and truncate to size_to_create
    int fd = open((rvm->directory + "/" + __FILE_PREPEND + segname).c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
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
    rvm->setMap(segname, mem);
    return mem;
}

/* unmap a segment from memory. */
void rvm_unmap(rvm_t rvm, void *segbase){
    // if segbase is not currently mapped, return immediately.
    if(rvm->eraseMap(segbase) == false){
        fprintf(stderr, "unmap %p failed\n", segbase);
        return;
    }
    free(segbase);
}

/* destroy a segment completely, erasing its backing store. This function should not be called on a segment that is currently mapped. */
void rvm_destroy(rvm_t rvm, const char *segname){
    // if segname is current mapped, return immediately
    if(rvm->getBase(segname) != NULL){
        fprintf(stderr, "destory segname %s failed\n", segname);
        return;
    }

    // delete disk hard-copy
    std::string filename = rvm->directory + "/" + __FILE_PREPEND + segname;
    unlink(filename.c_str());
}

/* begin a transaction that will modify the segments listed in segbases. If any of the specified segments is already being modified by a transaction, then the call should fail and return (trans_t) -1. Note that trans_t needs to be able to be typecasted to an integer type. */
trans_t rvm_begin_trans(rvm_t rvm, int numsegs, void **segbases){
    // If any segbases is modified by other transaction, return immediately
    for(int i=0; i<numsegs; i++){
        for(unsigned int j=0; j<rvm->transaction.size(); j++){
            RVM_transaction* transaction = rvm->transaction[j];
            if(transaction->find(segbases[i]) >= 0){
                fprintf(stderr, "begin trans %p failed\n", segbases[i]);
                return (trans_t) -1;
            }
        }
    }

    RVM_transaction *transaction = new RVM_transaction(numsegs, segbases);
    rvm->transaction.push_back(transaction);
    printf("trans[%d] begin\n", transaction->getID());
    return transaction->getID();
}

/* declare that the library is about to modify a specified range of memory in the specified segment. The segment must be one of the segments specified in the call to rvm_begin_trans. Your library needs to ensure that the old memory has been saved, in case an abort is executed. It is legal call rvm_about_to_modify multiple times on the same memory area. */
void rvm_about_to_modify(trans_t tid, void *segbase, int offset, int size){
    RVM_transaction *transaction = rvm->getTransaction(tid);
    if(transaction != NULL){
        int i = transaction->find(segbase);
        printf("\tmodify\t[%s]\toffset: %d, size: %d\n", rvm->getName(segbase), offset, size);
        transaction->log.push_back(new log_t(i, offset, size, segbase));
    }
}

/* commit all changes that have been made within the specified transaction. When the call returns, then enough information should have been saved to disk so that, even if the program crashes, the changes will be seen by the program when it restarts. */
void rvm_commit_trans(trans_t tid){
    RVM_transaction *transaction = rvm->getTransaction(tid);
    if(transaction != NULL){
        int logNum = transaction->log.size();
        fwrite("TB", 1, 2, rvm->log_file);
        fwrite(&logNum, sizeof(int), 1, rvm->log_file);
        for(std::vector<log_t*>::iterator it = transaction->log.begin(); it != transaction->log.end(); it++) {
            log_t* record = *it;
            
            void *mem = transaction->getSeg(record->segID);
            int offset = record->offset;
            int length = record->length;

            const char* segname = rvm->getName(mem);
            printf("\tcommit\t[%s]\toffset: %d, size: %d\n", segname, offset, length);
            fwrite(segname, sizeof(char), strlen(segname) + 1, rvm->log_file);
            fwrite(&offset, sizeof(int), 1, rvm->log_file);
            fwrite(&length, sizeof(int), 1, rvm->log_file);
            fwrite((char*) mem + offset, sizeof(char), length, rvm->log_file);

            delete record;
            setDirty(rvm, segname, true);
        }
        fwrite("TE", 1, 2, rvm->log_file);
        fflush(rvm->log_file);
        
        transaction->log.erase(transaction->log.begin(), transaction->log.end());
        delete transaction;
    }
}

/* undo all changes that have happened within the specified transaction. */
void rvm_abort_trans(trans_t tid){
    RVM_transaction *transaction = rvm->getTransaction(tid);
    if(transaction != NULL){
        while(!transaction->log.empty()){
            // Revert back and free memory
            log_t *log = transaction->log.back();
            transaction->log.pop_back();
            void *segbase = transaction->getSeg(log->segID);
            int offset = log->offset;
            int length = log->length;
            memcpy((char*) segbase + offset, log->mem, length);
            printf("\tundo\t[%s]\toffset: %d, size: %d\n", rvm->getName(segbase), offset, length);
            delete log;
        }
        delete transaction;
    }
}

/* play through any committed or aborted items in the log file(s) and shrink the log file(s) as much as possible. */
void rvm_truncate_log(rvm_t rvm){
    unsigned long int id;
    printf("try to truncate log...\n");
    for (id = rvm->log_id_min; id < rvm->log_id; id++) {
        printf("truncate .log%lu\n", id);
        truncateLog(rvm, id);
    }
    rvm->log_id_min = rvm->log_id;
    printf("done\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Utility functions */
int checkTransaction(rvm_t rvm, char *cur, char *end){
    //EOF
    if (cur > end) return -1;

    //TB
    if (cur + 1 > end || *cur != 'T' || *(cur + 1) != 'B') {
        fprintf(stderr, "checkTransaction error: TB\n");
        return -1;
    }
    cur += 2;

    //numlog
    if (cur + sizeof(int) - 1 > end) {
        fprintf(stderr, "checkTransaction error: numlog\n");
        return -1;
    }
    int numlog = *((int*)cur);
    cur += sizeof(int);

    int nameLen;
    int length;
    while(numlog) {
        //segname
        nameLen = 0;
        while(cur <= end && *cur != '\0') {
            nameLen++;
            cur++;
        }
        if(cur > end || !nameLen) {
            printf("%d\n", nameLen);
            fprintf(stderr, "checkTransaction error: segname\n");
            return -1;
        }
        cur++;

        //offset & length
        if (cur + sizeof(int)*2 - 1 > end) {
            fprintf(stderr, "checkTransaction error: offset or length\n");
            return -1;
        }
        cur += sizeof(int); //offset
        length = *((int*)cur);
        cur += sizeof(int);

        //data
        cur += length;
        if(cur > end) {
            fprintf(stderr, "checkTransaction error: data\n");
            return -1;
        }

        numlog--;
    }

    //TE
    if (cur + 1 > end || *cur != 'T' || *(cur + 1) != 'E') {
        fprintf(stderr, "checkTransaction error: TE\n");
        return -1;
    }

    return 0;
}

char* redoTransaction(rvm_t rvm, char *cur, char *end){
    char seg_path[strlen(rvm->directory.c_str()) + 20];
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
            sprintf(seg_path, "%s/%s%s", rvm->directory.c_str(), __FILE_PREPEND, segname);
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

void truncateLog(rvm_t rvm, unsigned long int id){
    char log_path[strlen(rvm->directory.c_str()) + 20];
    int fd;
    struct stat sb;
    char *addr;
    char *cur;
    char *end;

    sprintf(log_path, "%s/.log%lu", rvm->directory.c_str(), id);
    fd = open(log_path, O_RDONLY);
    if (fd == -1 || fstat(fd, &sb) == -1){
        perror("ERROR:");
        fprintf(stderr, "truncate %s failed\n", log_path);
        return;
    }

    cur = addr = (char*)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    end = addr + sb.st_size - 1;
    while(cur) {
        cur = redoTransaction(rvm, cur, end);
    }
    munmap(addr, sb.st_size);
    close(fd);
    unlink(log_path);
}

bool isDirty(rvm_t rvm, std::string segname){
    return rvm->dirtyMap[segname];
}

void setDirty(rvm_t rvm, std::string segname, bool dirty){
    if(!dirty) 
        rvm->dirtyMap.erase(segname);

    rvm->dirtyMap[segname] = dirty;
}

void resetLog(rvm_t rvm){
    rvm->log_id_min = rvm->log_id = 0;

    // Update logID
    std::string logId_path = rvm->directory + "/.rvm_logID";
    FILE *logID_file = fopen(logId_path.c_str(), "w");
    fprintf(logID_file, "%lu", rvm->log_id + 1);
    fclose(logID_file);

    // Create Log file
    char log_path[strlen(rvm->directory.c_str()) + 20];
    sprintf(log_path, "%s/.log%lu", rvm->directory.c_str(), rvm->log_id);
    rvm->log_file = fopen(log_path, "wb+");
}

RecoverableVM::~RecoverableVM(){
    //abort remaining transaction
    for(unsigned int i=0; i<rvm->transaction.size(); i++){
        rvm_commit_trans(rvm->transaction[0]->getID());
    }

    //unmap remaining segbase, segname
    while(!rvm->segnameMap.empty()){
        rvm_unmap(rvm, (void*)rvm->segnameMap.begin()->first);
    }

    fclose(rvm->log_file);
}
RVM_transaction::~RVM_transaction(){
    // Free remaining log record
    while(!log.empty()){
        log_t *record = log.back();
        log.pop_back();
        delete record;
    }

    // Remove transaction from rvm->transcation list
    for(std::vector<RVM_transaction*>::iterator it = rvm->transaction.begin(); it != rvm->transaction.end(); it++){
        if(id == (*it)->id){
            rvm->transaction.erase(it);
            break;
        }
    }

    printf("trans[%d] finished\n", id);
}
log_t::~log_t(){
    free(mem);
}

const void *RecoverableVM::getBase(std::string segname){
    std::map<std::string, void*>::iterator it = rvm->segbaseMap.find(segname);

    if(it == rvm->segbaseMap.end()) return NULL;
    else return it->second;
}
const char *RecoverableVM::getName(void *segbase){
    std::map<void*, std::string>::iterator it = rvm->segnameMap.find(segbase);

    if(it == rvm->segnameMap.end()) return NULL;
    else return it->second.c_str();
}
RVM_transaction* RecoverableVM::getTransaction(trans_t tid){
    for(std::vector<RVM_transaction*>::iterator it = transaction.begin(); it != transaction.end(); it++){
        if((*it)->getID() == tid)
            return *it;
    }
    return NULL;
}
void RecoverableVM::setMap(std::string name, void* mem){
    segnameMap[mem] = name;
    segbaseMap[name] = mem;
}
bool RecoverableVM::eraseMap(void* mem){
    const char *segname = getName(mem);
    if(segname == NULL) return false;

    segnameMap.erase(mem);
    segbaseMap.erase(segname);
    return true;
}

int RVM_transaction::find(void* mem){
    for(int k=0; k<numsegs; k++)
        if(mem == segbases[k]) return k;
    return -1;
}
trans_t RVM_transaction::getID(){
    return id;
}
void *RVM_transaction::getSeg(int k){
    if(k >= numsegs) return NULL;
    return segbases[k];
}
