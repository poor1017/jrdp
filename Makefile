
CFILES 	= server.c jrdp.c
OBJECTS = $(CFILES:.c=.o)
CC=gcc
CC_LINK=$(CC)
CFLAGS += -g

all: server

server: server.o jrdp.o
	@${CC_LINK} ${CFLAGS} -I ./ -o $@ server.o jrdp.o
	@echo "Compile successfully!"	

clean:
	@$(RM) client server $(OBJECTS)
