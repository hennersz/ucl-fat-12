CFLAGS = -g -Wall

ALL:	dos_ls dos_cp dos_scandisk
.PHONY: ALL clean

dos_ls:	dos_ls.o dos.o
	$(CC) $(CFLAGS) -o dos_ls dos_ls.o dos.o

dos_cp:	dos_cp.o dos.o
	$(CC) $(CFLAGS) -o dos_cp dos_cp.o dos.o

dos_scandisk: dos_scandisk.o dos.o
	$(CC) $(CFLAGS) -o dos_scandisk dos_scandisk.o dos.o

test: 
	mkdir ../testing
	cp ../images/*.img ../testing
	./dos_scandisk ../testing/floppy.img
	./dos_scandisk ../testing/badfloppy1.img
	./dos_scandisk ../testing/badfloppy2.img


clean:
	rm -f dos.o dos_ls dos_ls.o dos_cp dos_cp.o dos_scandisk dos_scandisk.o
	rm -rf ../testing
