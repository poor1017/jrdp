
CFILES 	= server.c client.c jrdp.c
OBJECTS = $(CFILES:.c=.o)
CC=gcc
CC_LINK=$(CC)
CFLAGS += -g

all: server client
	@echo "Compile successfully!"

server: server.o jrdp.o
	@${CC_LINK} ${CFLAGS} -I ./ -o $@ server.o jrdp.o

client: client.o jrdp.o
	@${CC_LINK} ${CFLAGS} -I ./ -o $@ client.o jrdp.o


clean:
	@$(RM) client server $(OBJECTS)
	@echo "Clean successfully!"
