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
