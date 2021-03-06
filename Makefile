#### RVM Library Makefile

CFLAGS  = -Wall -g
LFLAGS  =
CC      = g++
RM      = /bin/rm -rf
AR      = ar rc
RANLIB  = ranlib

LIBRARY = librvm.a

LIB_SRC = rvm.cpp
LIB_OBJ = $(patsubst %.cpp,%.o,$(LIB_SRC))

all: $(LIBRARY) test

%.o: %.cpp
	$(CC) -c $(CFLAGS) $< -o $@

$(LIBRARY): $(LIB_OBJ)
	$(AR) $(LIBRARY) $(LIB_OBJ)
	$(RANLIB) $(LIBRARY)

test:
	make -C testcases clean
	make -C testcases

clean:
	$(RM) $(LIBRARY) $(LIB_OBJ)
	make clean -C testcases
