TOPDIR=../..
include $(TOPDIR)/wvrules.mk

XPATH=../wvstreams/include

default: all

all: wvtftp.a wvtftpd

LIBS = ${EFENCE}

wvtftp.a: wvtftpbase.o wvtftpserver.o

wvtftpd: wvtftp.a ../wvstreams/libwvstreams.a

clean:
	rm -f wvtftpd backup.a *.o
