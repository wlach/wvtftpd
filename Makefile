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

wvtftpd-LIBS+= -L../wvstreams -lwvutils -lwvstreams -luniconf
wvtftpd: wvtftp.a

clean:
	rm -f wvtftpd
