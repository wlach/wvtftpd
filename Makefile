TOPDIR=../..
include $(TOPDIR)/rules.mk

XPATH=../utils ../streams ../ipstreams ../configfile

default: all wvtftp.a wvtftpd

all:

#LIBS = ${EFENCE}

wvtftp.a: wvtftpbase.o wvtftpserver.o

wvtftpd: wvtftp.a ../configfile/configfile.a ../streams/streams.a \
          ../ipstreams/ipstreams.a ../utils/utils.a ../configfile/configfile.a

clean:
	rm -f wvtftpd backup.a *.o
