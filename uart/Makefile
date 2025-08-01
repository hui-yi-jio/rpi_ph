TARGET = uart
CC = clang 

all : $(TARGET) 
	./$(TARGET)

main : $(TARGET).c 
	$(CC) $< -o $@ $(LDFLAGS)

clean :
	rm $(TARGET) 
