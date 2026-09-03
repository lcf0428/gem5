# CC      = musl-gcc
# CFLAGS  = -O1 -Wall -Wextra -g -static -fno-inline-small-functions

# TARGET  = victim

# all: $(TARGET)

# $(TARGET): main.c ngx_auth.c ngx_pnalloc.c stubs.c \
#            types.h config.h constants.h stubs.h
# 	$(CC) $(CFLAGS) -o $@ main.c

# clean:
# 	rm -f $(TARGET)

# .PHONY: all clean

CC      = musl-gcc

CFLAGS  = -O1 -Wall -Wextra -g -static \
          -fno-inline-small-functions \
          -fno-builtin-memcpy \
          -I /root/gem5/gem5/include

LDFLAGS = -L /root/gem5/gem5/util/m5/build/x86/out

LDLIBS  = -lm5

TARGET  = victim

all: $(TARGET)

$(TARGET): main.c ngx_auth.c ngx_pnalloc.c stubs.c \
           types.h config.h constants.h stubs.h
	$(CC) $(CFLAGS) -o $@ main.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean