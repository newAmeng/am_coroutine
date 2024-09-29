target = am_server am_client
src = $(wildcard *.cpp)
obj = $(patsubst %.cpp, %.o, $(src))

CC = g++
CPPFLAGS = -I /usr/include/mysql
LDFLAGS = -L /usr/lib64/mysql
LIBRARY = -lpthread -lmysqlclient -ldl


all : $(target)
.PHONY : all

am_server : am_server.o am_coroutine.o am_socket.o am_schedule.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBRARY)

am_client : am_client.o
	$(CC) -o $@ $^
	
	

clean :
	rm -rf $(obj) $(target)

