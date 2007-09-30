/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2005 Net Integration Technologies, Inc.
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
#include <uniconfroot.h>
#include <wvstreamsdaemon.h>
#include "wvtftpserver.h"
#include <signal.h>
#include <errno.h>

#include "version.h"


class WvTFTPDaemon : public WvStreamsDaemon
{
public:
    WvTFTPDaemon()
	: WvStreamsDaemon("wvtftpd", WVTFTP_VER_STRING, 
			  wv::bind(&WvTFTPDaemon::cb, this)),
          cfgmoniker("ini:/etc/wvtftpd.conf"), log("wvtftpd", WvLog::Info)
    {
	args.add_option('c', "config", "Config file",
			"ini:filename.ini", cfgmoniker);
    }
    
    virtual ~WvTFTPDaemon()
    {
	cfg.commit();
    }
    
    void cb()
    {
	cfg.unmount(cfg.whichmount(), true); // just in case
	cfg.mount(cfgmoniker);
	if (!cfg.whichmount() || !cfg.whichmount()->isok())
	{
	    log(WvLog::Error,
		"Can't read configuration from '%s'! Aborting.\n",
		cfgmoniker);
	    return;
	}
	
        tftps = new WvTFTPServer(cfg, 100);
        add_die_stream(tftps, true, "WvTFTP");
    }

private:
    WvString cfgmoniker;
    UniConfRoot cfg;
    WvTFTPServer *tftps;
    WvLog log; 
};


int main(int argc, char **argv)
{
    return WvTFTPDaemon().run(argc, argv);
}
