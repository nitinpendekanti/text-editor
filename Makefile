CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o) 

TARGET = main

all: $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
