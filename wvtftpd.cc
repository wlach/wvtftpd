/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2001 Net Integration Technologies, Inc.
 *
 * This is the WvTFTP server daemon.
 */
#include "wvtftpserver.h"

int main()
{
    WvStringList *tftpdirs = new WvStringList;
    tftpdirs->append(new WvString("/tftpboot"),true);
    WvTFTPServer tftps(tftpdirs, 30, 30);

    while (tftps.isok())
    {
	if (tftps.select(0))
	    tftps.callback();
    }
    wvcon->print("WvTFTPServer is not okay; aborting.\n");
}
