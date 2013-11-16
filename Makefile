# simple makefile to just get things working under linux

CPPOPTS=-std=c++0x -g3

all: test_capture_db

capture_db.o: capture_db.h capture_db.cc
	g++ $(CPPOPTS) -o $@ -c capture_db.cc

test_capture_db: capture_db.o test_capture_db.cc
	g++ $(CPPOPTS) -o $@ test_capture_db.cc capture_db.o -lrt -lsqlite3
