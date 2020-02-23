DESTDIR ?=

.PHONY: all
all: init

.PHONY: install
install: init
	install -s init $(DESTDIR)/

.PHONY: clean
clean:
	rm -f init
