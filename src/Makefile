BUILD_DIR = ../build

$(BUILD_DIR)/cute: $(BUILD_DIR)/cute.tab.c $(BUILD_DIR)/cute.yy.c vm/vm.cpp
	g++ -o $(BUILD_DIR)/cute -I interpreter -I vm -I std $(BUILD_DIR)/cute.tab.c $(BUILD_DIR)/cute.yy.c vm/vm.cpp std/misc.cpp

$(BUILD_DIR)/cute.yy.c: interpreter/cute.l
	flex -o $(BUILD_DIR)/cute.yy.c interpreter/cute.l

$(BUILD_DIR)/cute.tab.c: interpreter/cute.y
	bison -o $(BUILD_DIR)/cute.tab.c -d interpreter/cute.y

clean:
	rm $(BUILD_DIR)/*
