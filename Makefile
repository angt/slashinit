CC     = cc
CFLAGS = -Wall -O2

init:
	$(CC) $(CFLAGS) $(CPPFLAGS) init.c -o init

install: init
	mkdir -p $(DESTDIR)
	mv -f init $(DESTDIR)/

clean:
	rm -f init

.PHONY: init install clean
