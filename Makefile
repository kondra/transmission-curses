#EXES = $(basename $(wildcard *.c))
CC = gcc
CFLAGS = -Wall -g

OBJ = remote.o main.o

TARGET = main 

#all: 
#	$(MAKE) $(EXES)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

remote.o: remote.h
main.o: remote.h

clean:
	rm -f $(TARGET) *.o 
