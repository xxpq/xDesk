VERSION = 2.1.0 beta
SDL2  = $(shell sdl2-config --cflags --libs)
FLAGS = -I./src -Ofast -lpthread $(SDL2) -llz4 -fno-exceptions -std=c++17 -DVERSION="\"$(VERSION)\""

ifeq ($(shell uname -s), Linux)
	DISPLAY = ./src/display/x11.cpp ./src/display/wayland.cpp ./src/display/wayland/*.cpp
	LIBS 	= -lX11 -lXext -lXtst `pkg-config --cflags --libs libportal libpipewire-0.3`
else ifeq ($(shell uname -s), Darwin)
	DISPLAY = ./src/display/osx.cpp
	LIBS 	=
else
	DISPLAY = ./src/display/d3d11.cpp
	LIBS 	= -ld3d11
endif

NET_V2 = ./src/net/v2/ikcp.c ./src/net/v2/crc32.cpp ./src/net/v2/tcp_stack.cpp ./src/net/v2/kcp_stack.cpp ./src/net/v2/dispatcher.cpp ./src/net/v2/manager.cpp
SRC = ./src/main.cpp ./src/args.cpp ./src/client.cpp ./src/server.cpp ./src/net.cpp ./src/net_adapter_v2.cpp ./src/service.cpp ./src/codec.cpp ./src/display.cpp ./src/codec/*.cpp ./src/client/*.cpp $(DISPLAY) $(NET_V2) $(FLAGS) $(LIBS) -o deskx

all:
	g++ $(SRC)

test:
	g++ -DTEST $(SRC)-test

dpkg:
	cd deb; ./build.sh $(VERSION)
