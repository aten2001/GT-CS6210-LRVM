CFLAGS  = -Wall -g -I.
LFLAGS  =
CC      = g++
RM      = /bin/rm -rf
AR      = ar rc
RANLIB  = ranlib

LIBRARY = librvm.a

BIN = bin
SRC = $(wildcard *.c)
EXE = $(patsubst %.c,$(BIN)/%,$(SRC))

# All: $(patsubst %.cpp,%.o)
#@echo $(patsubst %.c,%.o,$(SRC))
all: $(BIN) $(EXE)
	@echo $(OBJ)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/%: %.c
	$(CC) $(CFLAGS) -I ../ $< ../$(LIBRARY) -o $@

clean:
	$(RM) $(BIN)


