CPP = /usr/bin/g++
CFLAGS = -Werror -std=c++17
LIBS_SO = -lpthread

HEADERS_DIR = ./header
OBJ_DIR = ./obj
TEST_DIR = ./test
SRC_DIR = ./src

TEST_BIN = $(TEST_DIR)/test
SRC_FILE = $(SRC_DIR)/threadpool.cpp
TEST_FILE = $(TEST_DIR)/test_threadpool.cpp
THREADPOOL_SO_FILE = $(OBJ_DIR)/libtdpool.so

.PHONY:test_code obj_so test_so

#源码编译测试程序
test_code:
	$(CPP) $(CFLAGS) -I$(HEADERS_DIR) -o $(TEST_BIN) $(SRC_FILE) $(TEST_FILE) $(LIBS_SO)

#编译线程池动态库
CFLAGS_OBG += $(CFLAGS) -fPIC -shared
obj_so:
	$(CPP) $(CFLAGS_OBG) -I$(HEADERS_DIR) $(SRC_FILE) -o $(THREADPOOL_SO_FILE)

#使用线程池动态库编译测试程序
test_so:
	$(CPP) $(CFLAGS) -I$(HEADERS_DIR) -L$(OBJ_DIR) -o $(TEST_BIN) $(TEST_FILE) -ltdpool $(LIBS_SO)