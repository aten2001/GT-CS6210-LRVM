#ifndef __LIBRVM_INTERNAL__
#define __LIBRVM_INTERNAL__

#include <vector>
#include <map>
#include <string>
#include <string.h>

#define __FILE_PREPEND "__RVM_"
typedef int trans_t;

class log_t{
public:
    log_t(int id, int offset, int length, void* original){
        this->segID = id;
        this->offset = offset;
        this->length = length;

        this->mem = new char[length];
        memcpy(this->mem, (char*)original+offset, length);
    }

    ~log_t();
    int segID;
    int offset;
    int length;
    void *mem;
};

class RVM_transaction {
public:
    RVM_transaction(int num, void** bases){
        id = max_id++;
        numsegs = num;
        segbases = bases;
    }

    ~RVM_transaction();
    bool find(void* mem);
    trans_t getID();
    void *getSeg(int k);

    std::vector<log_t*> log;
private:
    static trans_t max_id;
    trans_t id;
    int numsegs;
    void **segbases;
};

class RecoverableVM{
public:
    ~RecoverableVM();

    RVM_transaction* getTransaction(trans_t tid);
    const void* getBase(std::string);
    const char* getName(void*);
    void setMap(std::string, void*);
    bool eraseMap(void*);

    std::string directory; //directory name
    unsigned long int log_id;
    unsigned long int log_id_min;
    std::map<std::string, bool> dirtyMap;
    std::vector<RVM_transaction*> transaction; // transaction stack
    FILE *log_file;
private:
    std::map<void*, std::string> segnameMap; // (key, value) = (segbase, segname) map
    std::map<std::string, void*> segbaseMap; // (key, value) = (segname, segbase) map
};

typedef RecoverableVM* rvm_t;

#endif
