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

.PHONY: all clean install uninstall

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
