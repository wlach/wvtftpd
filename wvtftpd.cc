/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2004 Net Integration Technologies, Inc.
 *
 * This is the WvTFTP server daemon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "uniconfroot.h"
#include "wvtftpserver.h"
#include "wvlogrcv.h"
#include "wvver.h"
#include <signal.h>
#include <errno.h>

static bool want_to_die = false;

void sighandler_die(int signum)
{
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
    WvLog::LogLevel lvl = WvLog::Info;
    int c;
    signal(SIGTERM, sighandler_die);
    signal(SIGHUP, sighandler_die);
    signal(SIGINT, sighandler_die);
    while ((c = getopt(argc, argv, "dV?")) >= 0)
    {
        switch(c)
        {
        case 'd':
            if (lvl <= WvLog::Info)
                lvl = WvLog::Debug1;
            else
                lvl = WvLog::Debug4;
            break;
        case 'V':
            fprintf(stderr, "WvTFTPd version %s\n", WVTFTP_VER_STRING);
            exit(2);
        case '?':
            usage(argv[0]);
            break;
        }
    }

    WvLog log("WvTFTP", WvLog::Critical);
    UniConfRoot cfg("ini:/etc/wvtftpd.conf");
    WvTFTPServer tftps(cfg, 100);
    WvLogConsole logdisp(2, lvl);

    while (tftps.isok() && !want_to_die)
    {
	if (tftps.select(1000))
	    tftps.callback();
    }
    if (!tftps.isok() && tftps.geterr())
    {
        log("%s.\n", strerror(tftps.geterr()));
        return tftps.geterr();
    }

    cfg.commit();
    return 0;
}
