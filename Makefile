# Build the C version:  make        (or by hand: gcc -std=c99 -O2 -o l l.c)
# Test it:              make test    (runs test_l.py against the C build)
# make's built-in default CC is cc, which MinGW lacks; gcc is on all targets
ifeq ($(origin CC),default)
CC = gcc
endif
CFLAGS ?= -std=c99 -O2 -Wall -Wextra
ifeq ($(OS),Windows_NT)
EXE = .exe
endif

l$(EXE): l.c
	$(CC) $(CFLAGS) -o $@ l.c

test: l$(EXE)
	L_BIN=$(CURDIR)/l$(EXE) py -3 test_l.py

clean:
	rm -f l l.exe

.PHONY: test clean
