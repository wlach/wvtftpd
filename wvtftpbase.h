/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * WvTFTPBase, the WvTFTP base class.  This class holds functions that are
 *     common to both server and client.
 */
#ifndef __WVTFTPBASE_H
#define __WVTFTPBASE_H

#include <stdio.h>
#include <time.h>
#include "wvstring.h"
#include "wvhashtable.h"
#include "wvudp.h"
#include "wvlog.h"
#include "wvtimestream.h"
#include "wvstringlist.h"

const int MAX_PACKET_SIZE = 65535;
const bool WVTFTP_DEBUG = false;

class WvTFTPBase : public WvUDPStream
{
public:
    enum TFTPDir {tftpread = 0, tftpwrite = 1};
    enum TFTPOpcode {RRQ = 1, WRQ = 2, DATA = 3, ACK = 4, ERROR = 5};
    enum TFTPMode {netascii = 0, octet, mail};

    // _tftp_tick and _def_timeout should be given in seconds.
    // _tftp_tick is converted to ms before being stored in
    // WvTFTPBase::tftp_tick.
    WvTFTPBase(int _tftp_tick, int _def_timeout, int port = 0);
    virtual ~WvTFTPBase();

    struct TFTPConn
    {
        WvIPPortAddr remote;        // remote's address and port
        WvString filename;          // filename of this connection
        TFTPDir direction;          // reading or writing?
        TFTPMode mode;              // mode (netascii or octet)
        FILE *tftpfile;             // the file being transferred
        size_t blksize;             // blocksize (RFC 2348)
        int timeout;                // timeout 
        unsigned int tsize;         // transfer size (RFC 2349)
        unsigned int pktclump;      // number of packets to send at once
        unsigned int unack;         // first unacked packet for writing data
        unsigned int lastsent;      // block number of last packet sent
        bool donefile;              // done reading from the file?
        bool send_oack;             // do we need to or did we send an OACK?
        time_t stamp;               // time that we last sent a packet
        char oack[512];             // Holds the OACK packet in case we need
        size_t oacklen;             //     to resend it.
        int numtimeouts;
	
	TFTPConn()
	    { tftpfile = NULL; }
	~TFTPConn()
	    { if (tftpfile) fclose(tftpfile); }
    };

    DeclareWvDict(TFTPConn, WvIPPortAddr, remote);
    void set_max_timeouts(int _max_timeout);

protected:
    TFTPConnDict conns;
    WvLog log;
    int tftp_tick;
    char packet[MAX_PACKET_SIZE];
    size_t packetsize;
    int def_timeout;
    int max_timeouts;

    virtual void new_connection() = 0;
    virtual void handle_packet();
    void send_data(TFTPConn *c, bool resend = false);
    void send_ack(TFTPConn *c, bool resend = false);
    void send_err(char errcode, WvString errmsg = "");

    void dump_pkt();
};

#endif // __WVTFTP_H

