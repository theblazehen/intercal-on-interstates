# INTERCAL on Interstates — top-level build.
# Sources are picked up by wildcard per directory, so each component owner
# only adds files to their own directory.
-include config.mk
PREFIX ?= /usr/local
CC ?= cc
CFLAGS ?= -O2 -g
WARN = -Wall -Wextra -Wno-unused-parameter
B = build

CORE_SRC   = $(wildcard runtime/core/*.c)
WEB_SRC    = $(wildcard runtime/web/*.c)
ICK_SRC    = $(wildcard compiler/*.c)
GROSS_SRC  = $(wildcard gross/*.c)
SQLITE_FLAGS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1

RT_OBJ = $(patsubst %.c,$(B)/obj/%.o,$(CORE_SRC) $(WEB_SRC)) $(B)/obj/vendor/sqlite/sqlite3.o

.PHONY: all clean install check dist
all: $(B)/ick $(B)/gross $(B)/lib/libioi.a $(B)/share/syslib.i $(B)/share/floatlib.i

$(B)/obj/vendor/sqlite/sqlite3.o: vendor/sqlite/sqlite3.c
	@mkdir -p $(dir $@)
	$(CC) -O2 $(SQLITE_FLAGS) -c $< -o $@

$(B)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) -Iinclude -Ivendor/sqlite -MMD -MP -c $< -o $@

$(B)/lib/libioi.a: $(RT_OBJ)
	@mkdir -p $(dir $@)
	rm -f $@ && ar rcs $@ $^

$(B)/ick: $(patsubst %.c,$(B)/obj/%.o,$(ICK_SRC))
	$(CC) $(CFLAGS) -o $@ $^

$(B)/gross: $(patsubst %.c,$(B)/obj/%.o,$(GROSS_SRC))
	$(CC) $(CFLAGS) -o $@ $^

$(B)/share/%.i: lib/%.i
	@mkdir -p $(dir $@)
	cp $< $@

# Headers travel with the library so ick can compile generated C anywhere.
all: $(B)/include/.stamp
$(B)/include/.stamp: $(wildcard include/*.h)
	@mkdir -p $(B)/include
	cp include/*.h $(B)/include/
	@touch $@

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/ioi $(DESTDIR)$(PREFIX)/include/ioi $(DESTDIR)$(PREFIX)/share/ioi
	install -m 755 $(B)/ick $(B)/gross $(DESTDIR)$(PREFIX)/bin/
	install -m 644 $(B)/lib/libioi.a $(DESTDIR)$(PREFIX)/lib/ioi/
	install -m 644 include/*.h $(DESTDIR)$(PREFIX)/include/ioi/
	install -m 644 lib/*.i $(DESTDIR)$(PREFIX)/share/ioi/

check: all
	@for t in tests/*/run.sh; do echo "== $$t"; sh $$t || exit 1; done

VERSION = 1.1
dist:
	git archive --prefix=ioi-$(VERSION)/ -o ioi-$(VERSION).tar.gz HEAD

clean:
	rm -rf $(B)

-include $(shell find $(B)/obj -name '*.d' 2>/dev/null)
