TOPDIR=../..
include $(TOPDIR)/rules.mk

XPATH=../utils ../streams ../ipstreams

default: all wvtftp.a wvtftpd

all:

#LIBS = ${EFENCE}

wvtftp.a: wvtftp.o wvtftps.o

wvtftpd: wvtftp.a ../ipstreams/ipstreams.a ../streams/streams.a \
                      ../utils/utils.a
clean:
	rm -f wvtftpd backup.a *.o
