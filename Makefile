CFLAGS+=$(shell sdl2-config --cflags)
LDFLAGS+=$(shell sdl2-config --libs)
all: engine;
clean: ; $(RM) engine
