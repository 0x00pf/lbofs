BINARIES= proc_mem process_vm1  process_vm2  process_vm3  process_vm4 process_vm5 process_vm6\
	  dlopen1 read_procmap game

all:$(BINARIES) backdoor.bin libtest.so libevil.so

$(BINARIES): %: %.c
		$(CC) -o $@ $<

libtest.so: libtest.c
	${CC} -shared -fpic -o $@ $<

libevil.so: libevil.c
	${CC} -shared -fpic -o $@ $<

backdoor.bin: backdoor.x86.asm
	nasm -felf64 -o $@.o $<
	objcopy --dump-section .text=$@ $@.o

clean: 
	rm -f $(BINARIES) backdoor.bin backdoor.bin.o libtest.so libevil.so
