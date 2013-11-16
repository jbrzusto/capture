# simple makefile to just get things working under linux

CPPOPTS=-std=c++11 -g3
USRP_INCLUDE=-I/home/radar/gnuradio/usrp/host/include -I/home/radar/gnuradio/usrp/firmware/include
USRP_LIBS=-L/home/radar/gnuradio/usrp/host/lib -lusrp

all: capture test_capture_db

capture_db.o: capture_db.h capture_db.cc
	g++ $(CPPOPTS) -o $@ -c capture_db.cc

test_capture_db: capture_db.o test_capture_db.cc
	g++ $(CPPOPTS) -o $@ test_capture_db.cc capture_db.o -lrt -lsqlite3

capture.o: capture.cc capture_db.h
	g++ $(CPPOPTS) $(USRP_INCLUDE) -o $@ -c capture.cc

capture: capture.o
	gcc -o $@ $^ $(USRP_LIBS) -lusb-1.0 -lboost_program_options -lboost_thread
