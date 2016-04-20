/*For undo and redo logs*/
typedef struct mod_t{
  int offset;
  int size;
  void *undo;
} mod_t;



typedef struct _segment_t* segment_t;
typedef struct _redo_t* redo_t;

typedef struct _trans_t* trans_t;
typedef struct _rvm_t* rvm_t;
typedef struct segentry_t segentry_t;

struct _segment_t{
  char segname[128];
  void *segbase;
  int size;
  trans_t cur_trans;
  steque_t mods;
};

struct _trans_t{
  rvm_t rvm;          /*The rvm to which the transaction belongs*/
  int numsegs;        /*The number of segments involved in the transaction*/
  segment_t* segments;/*The array of segments*/
};

/*For redo*/
struct segentry_t{
  char segname[128];
  int segsize;
  int updatesize;
  int numupdates;
  int* offsets;
  int* sizes;
  void *data;
};

/*The redo log */
struct _redo_t{
  int numentries;
  segentry_t* entries;
};



/* rvm */
struct _rvm_t{
  char prefix[128];   /*The path to the directory holding the segments*/
  int redofd;         /*File descriptor for the redo-log*/
  seqsrchst_t segst;  /*A sequential search dictionary mapping base pointers to segment names*/ 
};