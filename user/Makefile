CC?=i686-elf-gcc
CFLAGS=-ffreestanding -O2 -g -Wall -Wextra -nostdlib -nostartfiles -fno-pic -m32

all: userprog.elf ush.elf forktest.elf proctest.elf minitest.elf simplefork.elf

userprog.elf: start.o main.o link.ld
	$(CC) $(CFLAGS) -T link.ld -nostdlib -o $@ start.o main.o

start.o: start.S
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: main.c
	$(CC) $(CFLAGS) -c -o $@ $<

syscalls.o: syscalls.c
	$(CC) $(CFLAGS) -c -o $@ $<

forktest.o: forktest.c
	$(CC) $(CFLAGS) -c -o $@ $<

forktest.elf: syscalls.o forktest.o link.ld
	$(CC) $(CFLAGS) -T link.ld -nostdlib -o $@ syscalls.o forktest.o

proctest.o: proctest.c
	$(CC) $(CFLAGS) -c -o $@ $<

proctest.elf: start.o syscalls.o proctest.o link.ld
	$(CC) $(CFLAGS) -T link.ld -nostdlib -o $@ start.o syscalls.o proctest.o

minitest.o: minitest.c
	$(CC) $(CFLAGS) -c -o $@ $<

minitest.elf: start.o minitest.o link.ld
	$(CC) $(CFLAGS) -T link.ld -nostdlib -o $@ start.o minitest.o

simplefork.o: simplefork.c
	$(CC) $(CFLAGS) -c -o $@ $<

simplefork.elf: start.o simplefork.o link.ld
	$(CC) $(CFLAGS) -T link.ld -nostdlib -o $@ start.o simplefork.o

clean:
	rm -f *.o userprog.elf ush.elf forktest.elf proctest.elf minitest.elf simplefork.elf

ush.elf: start.o ush.o link.ld
	$(CC) $(CFLAGS) -T link.ld -nostdlib -o $@ start.o ush.o

ush.o: ush.c
	$(CC) $(CFLAGS) -c -o $@ $<
