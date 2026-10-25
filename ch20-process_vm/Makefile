BINARIES= proc_mem process_vm1  process_vm2  process_vm3 

all:$(BINARIES)

$(BINARIES): %: %.c
		$(CC) -o $@ $<

clean: 
	rm -f $(BINARIES)
