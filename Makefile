# Makefile for smash (Small Shell)
# ================================
# macOS: Requires Homebrew GCC (brew install gcc)
# Linux: Uses system g++

# Detect OS and set compiler + flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS: Use Homebrew GCC, no _XOPEN_SOURCE (causes conflicts)
    CXX = /opt/homebrew/bin/g++-15
    CXXFLAGS = -std=c++11 -Wall -Wextra -pedantic
else
    # Linux: Use system g++, need _XOPEN_SOURCE for nftw()
    CXX = g++
    CXXFLAGS = -std=c++11 -Wall -Wextra -pedantic -D_XOPEN_SOURCE=500
endif

TARGET = smash

# Source files
SRCS = smash.cpp SmallShell.cpp Commands.cpp JobList.cpp signals.cpp
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compile
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(OBJS) $(TARGET)

# Rebuild
rebuild: clean all

.PHONY: all clean rebuild
