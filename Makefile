ifeq ($(TOPDIR),)                                                                              
TOPDIR=.
PKGINC=/usr/include/wvstreams /usr/local/include/wvstreams
endif

include $(TOPDIR)/wvrules.mk

XPATH=.. ../wvstreams/include $(PKGINC)

default: all

all: wvtftp.a wvtftpd

LIBS+=${EFENCE}

wvtftp.a: wvtftpbase.o wvtftpserver.o

wvtftpd: wvtftp.a $(LIBUNICONF)

clean:
	rm -f wvtftpd
