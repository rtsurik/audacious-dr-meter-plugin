LIBS = `pkg-config --libs gtk+-3.0 audacious`
INCLUDE = `pkg-config --cflags gtk+-3.0 audacious`
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -fPIC -DPIC
LDFLAGS += -shared
AU_PLUGINS_DIR = `pkg-config --variable=general_plugin_dir audacious`
SO_LIB = dr_meter.so

.PHONY: all clean build

build: $(SO_LIB)

clean: 
	rm -f $(SO_LIB) *.o

$(SO_LIB): dr_meter.c
	$(CC) -c dr_meter.c -o dr_meter.o $(INCLUDE) $(CFLAGS) $(INCLUDE)
	$(CC) -c dr_playlist.c -o dr_playlist.o $(CFLAGS)
	$(CC) -o dr_meter.so $(LDFLAGS) $(LIBS) *.o

install: build
	cp dr_meter.so $(AU_PLUGINS_DIR)
