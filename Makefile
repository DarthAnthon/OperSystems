CXX      = g++
CXXFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS  = -shared

TARGET_LIB = libcaesar.so
COPY_PROG = secure_copy

.PHONY: all install

# cборка динамической библиотеки и программы
all: $(TARGET_LIB) $(TEST_PROG) $(COPY_PROG)

$(TARGET_LIB): libcaesar.cpp
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^
	
# $(TEST_PROG): testpr.cpp
	# $(CXX) $(CXXFLAGS) testpr.cpp -o $(TEST_PROG) -ldl
	
$(COPY_PROG): secure_copy.cpp
	$(CXX) $(CXXFLAGS) -pthread secure_copy.cpp -o $(COPY_PROG) -L. -lcaesar

# установка библиотеки в системную директорию
	sudo cp $(TARGET_LIB) /usr/local/lib/
	sudo ldconfig

# Очистка
clean:
	rm -f $(TARGET_LIB) $(COPY_PROG)
