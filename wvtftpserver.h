/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * WvTFTPServer.  This class implements the server end of WvTFTP.
 */
#ifndef __WVTFTPSERVER_H
#define __WVTFTPSERVER_H

#include "wvtftpbase.h"
#include "uniconf.h"

class WvTFTPServer : public WvTFTPBase
{
public:
    WvTFTPServer(UniConf &_cfg, int _tftp_tick);
    void add_dir(WvString dir);
    void rm_dir(WvString dir);
    virtual ~WvTFTPServer();

private:
    UniConf &cfg;
    virtual void execute();
    virtual void new_connection();
    int validate_access(TFTPConn *c, WvString &basedir);
};

#endif // __WVTFTPS_H
