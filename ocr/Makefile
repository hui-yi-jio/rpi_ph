TARGET = ocr 
CC = clang++ 
CCFLAGS =  -O3 -Wall -march=armv8-a -mtune=cortex-a76
CCFLAGS += -I/usr/include/opencv4
LDFLAGS = -lopencv_core  -lopencv_highgui -lopencv_imgproc -lopencv_videoio -lopencv_imgcodecs -lva -lva-drm -ltesseract

all : $(TARGET) 
	./$(TARGET)

$(TARGET) : $(TARGET).cpp 
	$(CC) $(CCFLAGS) $< -o $@ $(LDFLAGS)

clean :
	rm $(TARGET) 
