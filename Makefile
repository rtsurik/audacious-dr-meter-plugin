CC = gcc
LIBS = `pkg-config --libs gtk+-3.0 audacious`
INCLUDE = `pkg-config --cflags gtk+-3.0 audacious`
CFLAGS = -Wall -O2 -fPIC -DPIC
LDFLAGS = -shared


SO_LIB = dr_meter.so

.PHONY: all clean build


build: $(SO_LIB)

clean: 
	rm -f $(SO_LIB) *.o

$(SO_LIB): dr_meter.c
	$(CC) -c dr_meter.c -o dr_meter.o $(INCLUDE) $(CFLAGS) $(INCLUDE)
	$(CC) -o dr_meter.so -shared $(LDFLAGS) $(LIBS) *.o

install: build
	cp dr_meter.so /usr/lib/audacious/General/
