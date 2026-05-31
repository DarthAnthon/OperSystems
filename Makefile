CXX      = g++
CXXFLAGS = -Wall -Wextra -pedantic -fPIC -std=c++11
LDFLAGS  = -shared

TARGET_LIB = librc4.so
COPY_PROG  = secure_copy
TEST_PROG  = test_copy

INSTALL_PATH = /usr/local/lib

.PHONY: all install clean test help

# Основная цель - собрать все
all: $(TARGET_LIB) $(COPY_PROG)

# Сборка динамической библиотеки RC4
$(TARGET_LIB): rc4.cpp rc4.h
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ rc4.cpp

# Сборка программы secure_copy
$(COPY_PROG): secure_copy.cpp rc4.h $(TARGET_LIB)
	$(CXX) $(CXXFLAGS) -pthread secure_copy.cpp -o $(COPY_PROG) \
		-L. -lrc4 -Wl,-rpath,.

# Установка библиотеки в системную директорию
install: $(TARGET_LIB)
	@echo "Installing $(TARGET_LIB) to $(INSTALL_PATH)..."
	sudo cp $(TARGET_LIB) $(INSTALL_PATH)/
	sudo ldconfig
	@echo "Installation complete"

# Очистка сборочных артефактов
clean:
	rm -f $(TARGET_LIB) $(COPY_PROG) $(TEST_PROG) *.o

# Удаление установленной библиотеки
uninstall:
	@echo "Removing $(TARGET_LIB) from $(INSTALL_PATH)..."
	sudo rm -f $(INSTALL_PATH)/$(TARGET_LIB)
	sudo ldconfig
	@echo "Uninstallation complete"