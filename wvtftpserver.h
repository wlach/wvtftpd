/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2004 Net Integration Technologies, Inc.
 *
 * WvTFTPServer.  This class implements the server end of WvTFTP.
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

    /** Returns true if good filename.
     * First checks for aliases, basedir, and default file.
     * Puts new filename in c->filename.
     */
    bool check_filename(TFTPConn *c);

    /** Finds and processes TFTP options.
     * Sets TFTPConn options and sends option ACKs.
     * Returns the packet index of the byte following the last option
     * value.
     */
    unsigned int process_options(TFTPConn *c, unsigned int opts_start);

    /** Checks for old WvConf-style cfg and converts to new UniConf style.
     * Returns true if an update was actually performed.
     */
    bool update_cfg(WvStringParm oldsect, WvStringParm newsect);
};

#endif // __WVTFTPSERVER_H
