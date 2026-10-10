CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -pipe -Wall -Wextra -Wpedantic -Wformat=2 -Wformat-security -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fstack-clash-protection -fPIE -fno-inline -fno-inline-functions
LDFLAGS ?= -pie -Wl,-z,relro,-z,now,-z,noexecstack
LDLIBS ?= -lcrypto
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install test
all: veyra

veyra: src/veyra.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

install: veyra
	install -Dm755 veyra "$(DESTDIR)$(BINDIR)/veyra"

clean:
	rm -f veyra

test: veyra
	./veyra examples/hello.vey
	./veyra examples/basics.vey
	./veyra examples/security_audit.vey
	./veyra examples/advanced.vey
	./veyra examples/factorial.vey
	./tests/check.sh
