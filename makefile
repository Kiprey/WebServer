SOURCE  := $(wildcard *.cpp)
INCLUDE := 
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))

TARGET  := WebServer
CC      := g++
LIBS    := -lpthread
CFLAGS  := -std=c++11 -g3 -ggdb3 -Wall -O0 $(INCLUDE)
CXXFLAGS:= $(CFLAGS)

.PHONY : objs clean veryclean rebuild all
all : $(TARGET)
objs : $(OBJS)
rebuild: veryclean all
clean :
	rm -rf *.o
veryclean : clean
	rm -rf $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)
