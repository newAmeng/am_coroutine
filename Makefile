target = am_server am_client
src = $(wildcard *.cpp)
obj = $(patsubst %.cpp,%.o,$(src))

CC = g++
CPPFLAGS = -I /usr/include/mysql
LDFLAGS = -L /usr/lib64/mysql
LIBRARY = -lpthread -lmysqlclient -ldl

am_server : am_server.o am_coroutine.o am_socket.o am_schedule.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBRARY)

am_client : client_mulport_epoll.o am_coroutine.o am_socket.o
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBRARY)

%.o : %.cpp
	$(CC) -c $< $(CPPFLAGS)

all : $(target)
.PHONY: all

clean:
	rm -rf $(obj) $(target)
.PHONY: clean
