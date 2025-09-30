TARGET = udp_test.out
SRCS = ../ReadConfig.c test.c
OBJS = ../ReadConfig.o test.o

CXX = gcc
# CXX = arm-linux-gnueabihf-gcc
INCFLAGS = 
DEFINE = 
CXXFLAGS = -I../
CPPFLAGS = -O2 -Wall -Wextra -Wconversion -Wno-unused-result -pipe -lm $(INCFLAGS) $(DEFINE) 
#CPPFLAGS = -g -O0 -Wall -Wextra -Wconversion -pipe -lm $(INCFLAGS) $(DEFINE) -Wno-psabi

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^
	
%.o: %.c
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

all: clean $(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
