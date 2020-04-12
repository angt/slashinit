DESTDIR ?=

.PHONY: all
all: init

.PHONY: install
install: init
	cp init $(DESTDIR)/

.PHONY: clean
clean:
	rm -f init
