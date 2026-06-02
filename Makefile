CC ?= cc
CFLAGS ?= -Os -flto -ffunction-sections -fdata-sections -Wall -Wextra -std=c11
LDFLAGS ?= -static -flto -Wl,--gc-sections
INSTALL ?= install
DESTDIR ?=
LIBEXECDIR ?= /usr/lib/vautoshiftd
UNITDIR ?= /usr/lib/systemd/system
DEFAULTDIR ?= /etc/default

TARGET := vautoshiftd
SRCS := main.c parser.c hotplug.c
OBJS := $(SRCS:.c=.o)
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),x86_64)
ARCH := amd64
else
ARCH := $(UNAME_M)
endif
DISTNAME := vautoshiftd-$(VERSION)-linux-$(ARCH)
DISTDIR := dist/$(DISTNAME)
DISTTAR := dist/$(DISTNAME).tar.gz

.PHONY: all clean install uninstall dist

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET) vautoshiftd.service vautoshiftd.default
	$(INSTALL) -d "$(DESTDIR)$(LIBEXECDIR)"
	$(INSTALL) -m 0755 "$(TARGET)" "$(DESTDIR)$(LIBEXECDIR)/$(TARGET)"
	$(INSTALL) -d "$(DESTDIR)$(UNITDIR)"
	$(INSTALL) -m 0644 vautoshiftd.service "$(DESTDIR)$(UNITDIR)/vautoshiftd.service"
	$(INSTALL) -d "$(DESTDIR)$(DEFAULTDIR)"
	if [ ! -f "$(DESTDIR)$(DEFAULTDIR)/vautoshiftd" ]; then $(INSTALL) -m 0644 vautoshiftd.default "$(DESTDIR)$(DEFAULTDIR)/vautoshiftd"; fi

uninstall:
	rm -f "$(DESTDIR)$(LIBEXECDIR)/$(TARGET)"
	rm -f "$(DESTDIR)$(UNITDIR)/vautoshiftd.service"

dist: $(TARGET)
	rm -rf "$(DISTDIR)"
	mkdir -p "$(DISTDIR)/etc/default"
	cp "$(TARGET)" vautoshiftd.service "$(DISTDIR)/"
	cp vautoshiftd.default "$(DISTDIR)/etc/default/vautoshiftd"
	cp packaging/release/install.sh packaging/release/uninstall.sh "$(DISTDIR)/"
	chmod 755 "$(DISTDIR)/install.sh" "$(DISTDIR)/uninstall.sh" "$(DISTDIR)/$(TARGET)"
	( cd "$(DISTDIR)" && sha256sum vautoshiftd vautoshiftd.service etc/default/vautoshiftd install.sh uninstall.sh > SHA256SUMS )
	tar -czf "$(DISTTAR)" -C dist "$(DISTNAME)"
	@echo "Created $(DISTTAR)"
