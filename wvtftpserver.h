/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2003 Net Integration Technologies, Inc.
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
    void check_timeouts();
    int validate_access(TFTPConn *c);
    WvString check_aliases(TFTPConn *c);

    /** Returns true if good filename after checking for aliases, basedir, and default file.
     * Puts new filename in c->filename.
     */
    bool check_filename(TFTPConn *c);

    /** Finds and processes TFTP options.
     * Sets TFTPConn options and sends option ACKs.
     * Returns the packet index of the byte following the last option
     * value.
     */
    unsigned int process_options(TFTPConn *c, unsigned int opts_start);
};

#endif // __WVTFTPSERVER_H
