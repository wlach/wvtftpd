/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 *
 * WvTFTPBase, the WvTFTP base class.  This class holds functions that are
 *     common to both server and client.
 */
#ifndef __WVTFTPBASE_H
#define __WVTFTPBASE_H

#include "wvstring.h"
#include "wvhashtable.h"
#include "wvudp.h"
#include "wvlog.h"
#include "wvtimestream.h"
#include "wvstringlist.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

const int MAX_PACKET_SIZE = 65535;
const bool WVTFTP_DEBUG = false;

class PktTime
{
public:
    PktTime(int _pktclump);
    ~PktTime();
    
    void set(int pktnum, struct timeval &tv);
    struct timeval *get(int pktnum);

private:
    int idx;
    int pktclump;
    struct timeval *times;
};

class WvTFTPBase : public WvUDPStream
{
public:
    enum TFTPDir {tftpread = 0, tftpwrite = 1};
    enum TFTPOpcode {RRQ = 1, WRQ = 2, DATA = 3, ACK = 4, ERROR = 5};
    enum TFTPMode {netascii = 0, octet, mail};

    // _tftp_tick is in ms.
    WvTFTPBase(int _tftp_tick, int port = 0);
    virtual ~WvTFTPBase();

    struct TFTPConn
    {
        WvIPPortAddr remote;        // remote's address and port
        WvString filename;          // filename of this connection
        TFTPDir direction;          // reading or writing?
        TFTPMode mode;              // mode (netascii or octet)
        FILE *tftpfile;             // the file being transferred
        size_t blksize;             // blocksize (RFC 2348)
        int tsize;                  // transfer size (RFC 2349)
        int pktclump;               // number of packets to send at once
        int unack;                  // first unacked packet for writing data
        int lastsent;               // block number of last packet sent
        bool donefile;              // done reading from the file?
        bool send_oack;             // do we need to or did we send an OACK?
        char oack[512];             // Holds the OACK packet in case we need
        size_t oacklen;             //     to resend it.
        int numtimeouts;
        int rtt;                    // "Total" round-trip time accumulator.
        PktTime *pkttimes;
        int total_packets;          // Number of correct packets used to
	                            //     calculate average rtt.
	           
        int timed_out_ignore;       // block number of largest packet that
	                            //     has been retransmitted due to
				    //     timeout, and should thus be ignored
				    //     in rtt calculations.
	
	TFTPConn()
	    { tftpfile = NULL; }
	~TFTPConn()
	    { if (tftpfile) fclose(tftpfile); if (pkttimes) delete pkttimes; }
    };

    DeclareWvDict(TFTPConn, WvIPPortAddr, remote);

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

