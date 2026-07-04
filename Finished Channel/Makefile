CC ?= gcc

CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
LDLIBS = -lm

PROGRAMS = producer consumer
OBJECTS = producer.o consumer.o shared.o

.PHONY: all clean rebuild

all: $(PROGRAMS)

producer: producer.o shared.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

consumer: consumer.o shared.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

producer.o: producer.c producer.h shared.h
consumer.o: consumer.c consumer.h shared.h
shared.o: shared.c shared.h

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(PROGRAMS) $(OBJECTS)

rebuild: clean all
