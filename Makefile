ifeq ($(TOPDIR),)
TOPDIR=.
with_xplc=$(shell pkg-config --variable=libdir wvxplc)
WVSTREAMS_SRC=.
WVSTREAMS_LIB=$(shell pkg-config --variable=libdir libwvstreams)
WVSTREAMS_INC=$(shell pkg-config --variable=includedir libwvstreams)
else
XPATH=..
endif

include $(TOPDIR)/wvrules.mk

default: all

all: wvtftp.a wvtftpd

LIBS+=${EFENCE}

wvtftp.a: wvtftpbase.o wvtftpserver.o

wvtftpd-LIBS = $(LIBUNICONF)
wvtftpd: wvtftp.a

clean:
	rm -f wvtftpd
