TOPDIR=../..
include $(TOPDIR)/wvrules.mk

XPATH=../utils ../streams ../ipstreams ../configfile

default: all

all: wvtftp.a wvtftpd

#LIBS = ${EFENCE}

wvtftp.a: wvtftpbase.o wvtftpserver.o

wvtftpd: wvtftp.a ../libwvstreams.a

clean:
	rm -f wvtftpd backup.a *.o
