
all: bin

bin: mpdnotify

mpdnotify: main.c
	gcc -o mpdnotify `pkg-config --cflags dbus-1` -lpthread -lmpdclient `pkg-config --libs dbus-1` -g -Wall -Wextra main.c
