TARGET = udp
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c,%.o,$(SRCS))

CXX = gcc
# CXX = arm-linux-gnueabihf-gcc
INCFLAGS = 
DEFINE = 
CXXFLAGS = 
CPPFLAGS = -O2 -Wall -Wextra -Wno-unused-result -Wno-unused-parameter -pipe -lm $(INCFLAGS) $(DEFINE) 
#CPPFLAGS = -g -O0 -Wall -Wextra -Wconversion -pipe -lm $(INCFLAGS) $(DEFINE) -Wno-psabi

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^
	
%.o: %.c
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

all: clean $(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
