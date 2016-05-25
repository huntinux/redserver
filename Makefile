
TARGET = redserver

SRCS = $(wildcard ./*.cpp)
OBJS = $(patsubst ./%.cpp, obj/%.o, $(SRCS))
INCLUDES = .

COMFLAG = -Wall -O3 -Wno-deprecated -std=c++11
LDFLAGS = -Wall -O3 
LIBS = 

obj/%.o:./%.cpp
	g++ $(COMFLAG) -c $<  -o $@

all: obj $(OBJS)
	g++ $(LDFLAGS) $(OBJS) $(LIBS) -o $(TARGET)
	@echo "Build Successfully."

obj:
	test -d obj || mkdir obj

.PHONEY:clean
clean:
	rm -rf obj/*.o $(TARGET)
