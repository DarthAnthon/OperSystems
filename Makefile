CXX      = g++
CXXFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS  = -shared

TARGET_LIB = libcaesar.so
# TEST_PROG  = test_pr
COPY_PROG = secure_copy
# TEST_INPUT = input.txt
# TEST_OUT   = output.txt
# TEST_OUT2  = output2.txt

.PHONY: all install run 
# .PHONY: all install run test

# cборка динамической библиотеки и программы
all: $(TARGET_LIB) $(TEST_PROG) $(COPY_PROG)

$(TARGET_LIB): libcaesar.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	
# $(TEST_PROG): testpr.cpp
# $(CXX) $(CXXFLAGS) testpr.cpp -o $(TEST_PROG) -ldl
	
$(COPY_PROG): secure_copy.cpp
	$(CXX) $(CXXFLAGS) -pthread secure_copy.cpp -o $(COPY_PROG) -L. -lcaesar

# установка библиотеки в системную директорию
install: $(TARGET_LIB)
	sudo cp $(TARGET_LIB) /usr/local/lib/
	sudo ldconfig

#secure_copy: secure_copy.cpp libcaesar.so
#	$(CXX) $(CXXFLAGS) -pthread secure_copy.cpp -o secure_copy -L. -lcaesar

run: $(COPY_PROG)
	LD_LIBRARY_PATH=. ./secure_copy source.txt dest.txt X
	LD_LIBRARY_PATH=. ./secure_copy dest.txt dest2.txt X

# test: $(TEST_PROG)
# @echo "=== Encryption ==="
# ./$(TEST_PROG) ./$(TARGET_LIB) X $(TEST_INPUT) $(TEST_OUT)
# @echo "=== Decryption ==="
# ./$(TEST_PROG) ./$(TARGET_LIB) X $(TEST_OUT) $(TEST_OUT2)
# @echo "=== Comparison ==="
# @cmp $(TEST_INPUT) $(TEST_OUT2) && echo "TEST PASSED" || echo "TEST FAILED"