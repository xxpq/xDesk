VERSION = 2.1.0 beta
SDL2_CFLAGS =
SDL2_LIBS   =
FLAGS = -I./src -Ofast -lpthread -llz4 -fno-exceptions -std=c++17 -DVERSION="\"$(VERSION)\""

ifeq ($(shell uname -s), Linux)
	SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 2>/dev/null)
	SDL2_LIBS   = $(shell pkg-config --libs sdl2 2>/dev/null)
	ifeq ($(strip $(SDL2_LIBS)),)
		SDL2_CFLAGS = $(shell sdl2-config --cflags 2>/dev/null)
		SDL2_LIBS   = $(shell sdl2-config --libs 2>/dev/null)
	endif
	DISPLAY = ./src/display/x11.cpp ./src/display/wayland.cpp ./src/display/wayland/*.cpp
	LIBS 	= -lX11 -lXext -lXtst `pkg-config --cflags --libs libportal libpipewire-0.3`
else ifeq ($(shell uname -s), Darwin)
	SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 2>/dev/null)
	SDL2_LIBS   = $(shell pkg-config --libs sdl2 2>/dev/null)
	DISPLAY = ./src/display/osx.cpp
	LIBS 	=
else
	SDL2_CFLAGS = $(shell pkg-config --cflags sdl2 2>/dev/null)
	SDL2_LIBS   = $(shell pkg-config --libs sdl2 2>/dev/null)
	DISPLAY = ./src/display/d3d11.cpp
	LIBS 	= -ld3d11 -lws2_32
	SDL2_CFLAGS := $(filter-out -Dmain=SDL_main,$(SDL2_CFLAGS))
	SDL2_LIBS := $(filter-out -lSDL2main,$(SDL2_LIBS))
	SDL2_LIBS := $(filter-out -lmingw32,$(SDL2_LIBS))
	SDL2_LIBS := $(filter-out -mwindows,$(SDL2_LIBS))
	SDL2_LIBS := -lSDL2 $(SDL2_LIBS)
	FLAGS += -Umain
endif

NET_V2 = ./src/net/v2/ikcp.c ./src/net/v2/crc32.cpp ./src/net/v2/tcp_stack.cpp ./src/net/v2/kcp_stack.cpp ./src/net/v2/dispatcher.cpp ./src/net/v2/manager.cpp
SRC = ./src/main.cpp ./src/args.cpp ./src/client.cpp ./src/server.cpp ./src/net.cpp ./src/net_adapter_v2.cpp ./src/service.cpp ./src/codec.cpp ./src/display.cpp ./src/codec/*.cpp ./src/client/*.cpp $(DISPLAY) $(NET_V2) $(FLAGS) $(SDL2_CFLAGS) $(LIBS) $(SDL2_LIBS) -o deskx

all:
	g++ $(SRC)

test:
	g++ -DTEST $(SRC)-test

dpkg:
	cd deb; ./build.sh $(VERSION)
