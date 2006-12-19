# Worldvisions Weaver Software:
#   Copyright (C) 1997-2004 Net Integration Technologies, Inc.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

ifeq ($(TOPDIR),)
# Change the prefix below if you don't want the same prefix as libwvstreams'.
prefix=$(shell pkg-config --variable=prefix libwvstreams)
TOPDIR=.
WVSTREAMS_SRC=.
WVSTREAMS_LIB=$(shell pkg-config --variable=libdir libwvstreams)
WVSTREAMS_INC=$(shell pkg-config --variable=includedir libwvstreams)

# hacky... but we only need the static xplc library, and wvrules.mk wants to
# link with the dynamic, which isn't installed by default with WvStreams.
with_xplc=no
CXXFLAGS=$(shell pkg-config --cflags libwvstreams)
LIBS+=$(shell pkg-config --libs libwvstreams)
XPATH=. ..
else
XPATH=. .. $(WVSTREAMS_INC) $(TOPDIR)/src
endif

BINDIR=${prefix}/sbin
MANDIR=${prefix}/share/man

include $(TOPDIR)/wvrules.mk

default: all

all: wvtftp.a wvtftpd

LIBS+=${EFENCE}

wvtftp.a: wvtftpbase.o wvtftpserver.o

wvtftpd-LIBS = $(LIBUNICONF)
wvtftpd: wvtftp.a

install: all
	[ -d ${BINDIR}      ] || install -d ${BINDIR}
	[ -d ${MANDIR}      ] || install -d ${MANDIR}
	install -m 0755 wvtftpd ${BINDIR}
	[ -d ${MANDIR}/man8 ] || install -d ${MANDIR}/man8
	install -m 0644 wvtftpd.8 ${MANDIR}/man8

uninstall:
	rm -f ${BINDIR}/wvtftpd
	rm -f ${MANDIR}/man8/wvtftpd.8

test: all t/all.t
	$(WVTESTRUN) $(MAKE) runtests

runtests: t/all.t
	WVTEST_MAX_SLOWNESS=0 $(VALGRIND) t/all.t $(TESTNAME)
	WVTEST_MIN_SLOWNESS=1 t/all.t $(TESTNAME)

t/all.t: $(call objects,t) wvtftp.a $(LIBUNICONF)

clean:
	rm -f wvtftpd wvtftp.a

ifdef PKGSNAPSHOT
SNAPDATE=+$(shell date +%Y%m%d)
endif

dist-hook:
	sed -e "s/Version\: @PKGVER@/Version\: $(PKGVER)$(SNAPDATE)/" \
		redhat/wvtftpd.spec.in > redhat/wvtftpd.spec


.PHONY: clean all install uninstall

