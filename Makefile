CC=gcc
CFLAGS=-I./include -L./libs -Wall
CSOURCEFILES=$(wildcard src/*.c)
BUILDDIR=bin
LIBS=-lbass -lsqlite3 -lb64 -lcomdlg32 -lgdi32 -lopengl32
FFLIBS=-lavformat-60 -lm -latomic -lz -lsecur32 -lws2_32 -lavcodec-60 -liconv -lm -latomic -lmfuuid -lole32 -lstrmiids -lole32 -luser32 -lz -lswscale-7 -lm -latomic -lswresample-4 -lm -latomic -lavutil-58 -lm -luser32 -lbcrypt -latomic
EXENAME=$(BUILDDIR)/spikevideo.exe

all: $(BUILDDIR) spikevideo

spikevideo: $(CSOURCEFILES)
	$(CC) $(CSOURCEFILES) -o $(EXENAME) $(CFLAGS) $(FFLIBS) $(LIBS)

$(BUILDDIR):
	mkdir $(BUILDDIR)

clean: 
	rm $(EXENAME)