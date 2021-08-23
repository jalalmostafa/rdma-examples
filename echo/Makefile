LDFLAGS=-libverbs -lrdmacm
CFLAGS=-g -Wall

all: echo-server echo-client

echo-server: echo-server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

echo-client: echo-client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf *.o echo-server echo-client
