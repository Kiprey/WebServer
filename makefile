SOURCE  := $(wildcard *.cpp)
INCLUDE := 
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))

TARGET  := WebServer
CC      := g++
LIBS    := -lpthread -lpq
CFLAGS  := -std=c++11 -O2 $(INCLUDE) -I/usr/include/postgresql
CXXFLAGS:= $(CFLAGS)
ifeq ($(DEBUG), 1)
    CFLAGS  := $(CFLAGS) -g3 -ggdb3 -O0 
    CXXFLAGS:= $(CFLAGS)
else
    CFLAGS := $(CFLAGS) -DNDEBUG
    CXXFLAGS := $(CFLAGS)
endif

.PHONY : objs clean veryclean rebuild all
all : $(TARGET)
objs : $(OBJS)
rebuild: veryclean all
clean :
	rm -rf *.o
	rm -rf $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)
