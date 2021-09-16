LDFLAGS=-libverbs -lrdmacm -lpthread
CFLAGS=-g -Wall -std=gnu99

all: echo-server echo-client

echo-server: echo-server.c
	$(CC) $(CFLAGS) $< echo.h -o $@ $(LDFLAGS)

echo-client: echo-client.c
	$(CC) $(CFLAGS) $< echo.h -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf *.o echo-server echo-client
