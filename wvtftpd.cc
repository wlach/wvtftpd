/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * This is the WvTFTP server daemon.
 */
#include "wvtftpserver.h"
#include "wvlogrcv.h"
#include <signal.h>

static bool want_to_die = false;

void sighandler_die(int signum)
{
    fprintf(stderr, "Caught signal %d.\n", signum);
    want_to_die = true;
}

int main()
{
    signal(SIGTERM, sighandler_die);
    signal(SIGHUP, sighandler_die);
    signal(SIGINT, sighandler_die);
    WvConf cfg("/etc/wvtftpd.conf");
    WvTFTPServer tftps(cfg, 30, 30);
    WvLogConsole logdisp(2, WvLog::Debug);

    while (tftps.isok() && !want_to_die)
    {
	if (tftps.select(1000))
	    tftps.callback();
    }
    cfg.save();
}
