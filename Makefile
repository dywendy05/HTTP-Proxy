CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread

all: proxy

proxy: proxy.o cache.o csapp.o
	$(CC) $(CFLAGS) -pthread -o proxy proxy.o cache.o csapp.o

csapp.o: csapp.c csapp.h
	$(CC) $(CFLAGS) -c csapp.c

proxy.o: proxy.c csapp.h proxy.h cache.h
	$(CC) $(CFLAGS) -c proxy.c

cache.o: cache.c cache.h
	$(CC) $(CFLAGS) -c cache.c

submit:
	(make clean; cd ..; tar cvf proxylab.tar proxylab-handout)

clean:
	rm -f *~ *.o proxy core

logclean:
	echo " " > server_log |> header_log |> resp_log 

