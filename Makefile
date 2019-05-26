CFLAGS+=-g -std=c99 -pedantic -Wall -Wextra -I/usr/X11R6/include
LDFLAGS+=-L/usr/X11R6/lib -lxcb -lxcb-util

dwmstatus: dwmstatus.c
	$(CC) $(CFLAGS) dwmstatus.c $(LDFLAGS) -o $@
