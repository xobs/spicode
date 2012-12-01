SOURCES=gpio.c sd.c main.c net.c parse.c nand.c
OBJECTS=$(SOURCES:.c=.o)
HEADERS=$(wildcard *.h)
EXEC=spi
MY_CFLAGS += -Wall -O0 -g
MY_LIBS += -lpthread -lrt

all: $(OBJECTS)
	$(CC) $(LIBS) $(LDFLAGS) $(OBJECTS) $(MY_LIBS) -o $(EXEC)

clean:
	rm -f $(EXEC) $(OBJECTS)

%.o: %.c ${HEADERS}
	$(CC) -c $(CFLAGS) $(MY_CFLAGS) $< -o $@

