/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * This is the WvTFTP server daemon.
 */
#include "wvtftpserver.h"
#include "wvlogrcv.h"
#include "wvver.h"
#include <signal.h>

static bool want_to_die = false;

void sighandler_die(int signum)
{
    fprintf(stderr, "Caught signal %d.\n", signum);
    want_to_die = true;
}

static void usage(char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-d] [-dd]\n"
            "     -d   Print debug messages\n"
            "     -dd  Print lots of debug messages\n"
            "     -V   Print version and exit\n",
            argv0);
    exit(1);
}

int main(int argc, char **argv)
{
    WvLog::LogLevel lvl = WvLog::Debug1;
    int c;
    signal(SIGTERM, sighandler_die);
    signal(SIGHUP, sighandler_die);
    signal(SIGINT, sighandler_die);
    while ((c = getopt(argc, argv, "dV?")) >= 0)
    {
        switch(c)
        {
        case 'd':
            if (lvl <= WvLog::Debug1)
                lvl = WvLog::Debug4;
            else
                lvl = WvLog::Debug5;
            break;
        case 'V':
            fprintf(stderr, "WvTFTPd version %s\n",WVTFTP_VER_STRING);
            exit(2);
        case '?':
            usage(argv[0]);
            break;
        }
    }

    WvConf cfg("/etc/wvtftpd.conf");
    WvTFTPServer tftps(cfg, 2, 6);
    WvLogConsole logdisp(2, lvl);

    while (tftps.isok() && !want_to_die)
    {
	if (tftps.select(1000))
	    tftps.callback();
	else
	    cfg.flush();
    }
    cfg.save();
}
