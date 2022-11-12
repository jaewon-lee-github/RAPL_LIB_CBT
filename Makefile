include ../../common/make.config

# C compiler
CC = g++
CC_FLAGS = -g -O2 

all: rapl.o 
	ar -rcs librapl.a rapl.o

%.o: %.[cpp|h]
	$(CC) $(CC_FLAGS) $< -c 
	
clean:
	rm -f *.o *.a
