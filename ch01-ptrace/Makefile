BINARIES= ptrace1 ptrace2 ptrace3 ptrace4 ptrace5 ptrace6 ptrace7 ptrace8 \
	  test10 test1 test2-1 test2 test3-1 test3 test4 test6 \
	  test7 test8 test9 checksum sim_ptrace sim_ptrace1

all:$(BINARIES) libbypass_ptrace.so count integrity

$(BINARIES): %: %.c
		$(CC) -o $@ $<

integrity: integrity.c integrity.ld
	${CC} -g -o integrity integrity.c -Wl,integrity.ld

count: count.asm
	nasm -felf64 -o count.o count.asm
	ld -o count count.o

libbypass_ptrace.so: bypass_ptrace.c
	${CC} --shared -fpic -o libbypass_ptrace.so bypass_ptrace.c

clean: 
	rm -f $(BINARIES) libbypass_ptrace.so count count.o integrity 
