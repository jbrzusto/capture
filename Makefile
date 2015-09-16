# simple makefile to just get things working under linux

# for extra debugging output, add option -DDEBUG

CPPOPTS=-std=c++11 -fPIC -O3
COPTS=-fPIC -O3

# debug version
## CPPOPTS=-std=c++11 -fPIC -g3 -DDEBUG
## COPTS=-fPIC -g3

USRP_INCLUDE=-I/home/radar/gnuradio/usrp/host/include -I/home/radar/gnuradio/usrp/firmware/include
USRP_LIBS=-L/home/radar/gnuradio/usrp/host/lib -lusrp
LIBS=-lpthread -lrt -lboost_program_options -lboost_thread -lboost_filesystem -lboost_system

all: capture test_capture_db

clean:
	rm -f *.o capture test_capture_db

capture_db.o: capture_db.h capture_db.cc
	g++ $(CPPOPTS) -o $@ -c capture_db.cc

test_capture_db: capture_db.o test_capture_db.cc
	g++ $(CPPOPTS) -o $@ test_capture_db.cc capture_db.o -lrt -lsqlite3

capture.o: capture.cc capture_db.h
	g++ $(CPPOPTS) $(USRP_INCLUDE) -o $@ -c capture.cc

rpcapture.o: rpcapture.cc capture_db.h pulse_metadata.h
	g++ $(CPPOPTS) -o $@ -c rpcapture.cc

capture: capture.o capture_db.o
	gcc $(COPTS) -o $@ $^ $(USRP_LIBS) $(LIBS)

shared_ring_buffer.o: shared_ring_buffer.cc shared_ring_buffer.h
	g++ $(CPPOPTS) -o $@ -c shared_ring_buffer.cc

sweep_file_writer.o: sweep_file_writer.cc sweep_file_writer.h
	g++ $(CPPOPTS) -o $@ -c sweep_file_writer.cc

tcp_reader.o: tcp_reader.cc tcp_reader.h
	g++ $(CPPOPTS) -o $@ -c tcp_reader.cc

rpcapture: rpcapture.o sweep_file_writer.o shared_ring_buffer.o tcp_reader.o
	g++ $(COPTS) -o $@ $^ $(LIBS)

scan_converter.o: scan_converter.h scan_converter.cc
	g++ $(CPPOPTS) -o $@ -c scan_converter.cc

latest_pulse_timestamp.o: latest_pulse_timestamp.c
	gcc $(COPTS) -o $@ -c latest_pulse_timestamp.c

capture_lib.so: capture_lib.cc scan_converter.o latest_pulse_timestamp.o
	g++ $(CPPOPTS) -I /usr/share/R/include -o $@ -shared $^ -lpthread -lrt
