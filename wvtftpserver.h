/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2001 Net Integration Technologies, Inc.
 *
 * WvTFTPServer.  This class implements the server end of WvTFTP.
 */
#ifndef __WVTFTPSERVER_H
#define __WVTFTPSERVER_H

#include "wvtftpbase.h"
#include "wvconf.h"

class WvTFTPServer : public WvTFTPBase
{
public:
    WvTFTPServer(WvConf &_cfg, int _tftp_tick, int _def_timeout);
    void add_dir(WvString dir);
    void rm_dir(WvString dir);
    virtual ~WvTFTPServer();

private:
    WvConf &cfg;
    virtual void execute();
    virtual void new_connection();
    int validate_access(TFTPConn *c, WvString &basedir);
};

#endif // __WVTFTPS_H
