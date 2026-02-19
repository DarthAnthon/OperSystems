CXX      = g++
CXXFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS  = -shared

TARGET_LIB = libcaesar.so
TEST_PROG  = test_pr
TEST_INPUT = input.txt
TEST_OUT   = output.txt
TEST_OUT2  = output2.txt

.PHONY: all install test

# —борка динамической библиотеки и программы
all: $(TARGET_LIB)

$(TARGET_LIB): libcaesar.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^
	
$(TEST_PROG): testpr.cpp
	$(CXX) $(CXXFLAGS) testpr.cpp -o $(TEST_PROG) -ldl

# ”становка библиотеки в системную директорию
install: $(TARGET_LIB)
	sudo cp $(TARGET_LIB) /usr/local/lib/
	sudo ldconfig

test: $(TEST_PROG)
	@echo "=== Encryption ==="
	./$(TEST_PROG) ./$(TARGET_LIB) X $(TEST_INPUT) $(TEST_OUT)
	@echo "=== Decryption ==="
	./$(TEST_PROG) ./$(TARGET_LIB) X $(TEST_OUT) $(TEST_OUT2)
	@echo "=== Comparison ==="
	@cmp $(TEST_INPUT) $(TEST_OUT2) && echo "TEST PASSED" || echo "TEST FAILED"