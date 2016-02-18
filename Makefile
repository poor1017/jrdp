
CFILES 	= server.c client.c rudp.c
OBJECTS = $(CFILES:.c=.o)
CC=gcc
CC_LINK=$(CC)
CFLAGS += -g

all: server client
	@echo "Compile successfully!"

server: server.o rudp.o
	@${CC_LINK} ${CFLAGS} -I ./ -o $@ server.o rudp.o

client: client.o rudp.o
	@${CC_LINK} ${CFLAGS} -I ./ -o $@ client.o rudp.o


clean:
	@$(RM) client server $(OBJECTS)
	@echo "Clean successfully!"
