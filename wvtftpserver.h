/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2001 Net Integrations Technologies, Inc.
 *
 * WvTFTPServer.  This class implements the server end of WvTFTP.
 */
#ifndef __WVTFTPSERVER_H
#define __WVTFTPSERVER_H

#include "wvtftpbase.h"

class WvTFTPServer : public WvTFTPBase
{
public:
    WvTFTPServer(WvString _basedir, int _tftp_tick, int _def_timeout);
    virtual ~WvTFTPServer();

private:
    WvString basedir;

    virtual void execute();
    virtual void new_connection();
};

#endif // __WVTFTPS_H
