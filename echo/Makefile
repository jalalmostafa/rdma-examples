
all: echo-server echo-client

echo-server:
	gcc echo-server.c -g -libverbs -o echo-server 

echo-client:
	gcc echo-client.c -g -libverbs -o echo-client

clean:
	rm -rf *.o echo-server echo-client
