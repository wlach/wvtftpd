/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2001 Net Integration Technologies, Inc.
 *
 * This is the WvTFTP server daemon.
 */
#include "wvtftpserver.h"

int main()
{
    WvConf cfg("/etc/wvtftp.conf");
    WvTFTPServer tftps(cfg, 30, 30);

    while (tftps.isok())
    {
	if (tftps.select(0))
	    tftps.callback();
    }
    wvcon->print("WvTFTPServer is not okay; aborting.\n");
}
