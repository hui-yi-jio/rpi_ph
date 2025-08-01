TARGET = cam
CC = clang 
CCFLAGS = -O3 -Wall 
LDFLAGS = -lrt -lm

all : $(TARGET) 
	./$(TARGET)

main : $(TARGET).c 
	$(CC) $< -o $@ $(LDFLAGS)

clean :
	rm $(TARGET) 
