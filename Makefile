include ../../common/make.config

# C compiler
CC = g++
CC_FLAGS = -g -O2 -fpic -DDEBUG

all: rapl.o 
	#ar -rcs librapl.a rapl.o
	$(CC) -shared -o librapl.so rapl.o

%.o: %.cpp
	$(CC) $(CC_FLAGS) $< -c 
	
clean:
	rm -f *.o *.so *.a
