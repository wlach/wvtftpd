/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 */
#include "wvtftpbase.h"
#include "strutils.h"

PktTime::PktTime(int _pktclump)
{
    pktclump = _pktclump;
    times = new struct timeval[pktclump];
    idx = 0;
}

PktTime::~PktTime()
{
    delete[] times;
}

void PktTime::set(int pktnum, struct timeval &tv)
{
    assert(pktnum >= idx);
    if ((pktnum - idx) >= pktclump)
    {
        int d = pktnum - pktclump - idx + 1;
        idx += d;
        if (d < pktclump && d > 0)
        {
            memmove(times, times + d, (pktclump - d) * sizeof(struct
                timeval));
            memset(times + pktclump - d, 0, d * sizeof(struct timeval));
        }
        else if (d >= pktclump)
            memset(times, 0, pktclump * sizeof(struct timeval));
    }
    struct timezone tz;
    int res = gettimeofday(&times[pktnum - idx], &tz);
    if (res != 0)
    {
        WvLog log("PktTime", WvLog::Error);
        log("gettimeofday() failed!\n");
        times[pktnum - idx].tv_sec = 0;
        times[pktnum - idx].tv_usec = 0;
    } 
}

struct timeval *PktTime::get(int pktnum)
{
    if (pktnum < idx || pktnum > (idx + pktclump))
        return NULL;
    return &times[pktnum - idx];
}

WvTFTPBase::WvTFTPBase(int _tftp_tick, int _def_timeout, int port = 0)
    : WvUDPStream(port, WvIPPortAddr()), conns(5), log("WvTFTP", WvLog::Debug4),
      tftp_tick(_tftp_tick*1000), def_timeout(_def_timeout), max_timeouts(7)
{
}

WvTFTPBase::~WvTFTPBase()
{
}

void WvTFTPBase::set_max_timeouts(int _max_timeouts)
{
    max_timeouts = _max_timeouts;
}

void WvTFTPBase::dump_pkt()
{
    //log(WvLog::Debug5, "Packet:\n");
    //log(WvLog::Debug5, hexdump_buffer(packet, packetsize));
}

void WvTFTPBase::handle_packet()
{
    log(WvLog::Debug4, "Handling packet from %s\n", remaddr);

    TFTPConn *c = conns[remaddr];
    TFTPOpcode opcode = static_cast<TFTPOpcode>(packet[0] * 256 + packet[1]);

    if (opcode == ERROR)
    {
        log(WvLog::Debug, "Received error packet; aborting.\n");
        conns.remove(c);
        return;
    }

    if (c->direction == tftpread)
    {
        // Packet should be an ack.
        if (opcode != ACK)
        {
            log(WvLog::Debug, "Badly formed packet (read); aborting.\n");
            send_err(4);
            conns.remove(c);
            return;
        }

        unsigned int blocknum = static_cast<unsigned char>(packet[2]) * 256 +
            static_cast<unsigned char>(packet[3]);
        log(WvLog::Debug5, "Blocknum is %s; unack is %s; lastsent is %s.\n", blocknum,
            c->unack, c->lastsent);
        if (blocknum == 0 && c->send_oack)
        {
            c->send_oack = false;
            log(WvLog::Debug5, "last sent: %s unack: %s pktclump: %s\n",c->lastsent, c->unack,
                c->pktclump);
            int pktsremain = static_cast<int>(c->lastsent) -
                static_cast<int>(c->unack);
            while (pktsremain < static_cast<int>(c->pktclump) - 1)
            {
                log(WvLog::Debug5, "result is %s\n", pktsremain);
                log(WvLog::Debug5, "send\n");
                send_data(c);
                if (c->donefile)
                    break;
                pktsremain = static_cast<int>(c->lastsent) -
                    static_cast<int>(c->unack);
            }
        }
        else if ((!c->unack && blocknum == 65535) || (blocknum == c->unack - 1))
            send_data(c, true);    // Client probably timed out; resend packets.
        else if (((c->unack <= c->lastsent) && (blocknum >= c->unack)
                    && (blocknum <= c->lastsent))
                 || ((c->unack > c->lastsent)
                    && ((blocknum >= c->unack) || (blocknum <= c->lastsent))))
        {
            if (blocknum == c->lastsent && c->donefile)
            {
                log(WvLog::Debug, "File transferred successfully.\n");
                conns.remove(c);
            }
            else
            {
                c->stamp = time(0);
                while (((c->lastsent - c->unack) % 65536) < c->pktclump)
                {
                    if (c->donefile)
                        break;
                    send_data(c);
                }
                c->unack = blocknum + 1;
                if (c->unack == 65536)
                    c->unack = 0;
            }
        }
    }
    else
    {
        // Packet should be data.
        if (opcode != DATA)
        {
            log(WvLog::Debug, "Badly formed packet (write); aborting.\n");
            send_err(4);
            conns.remove(c);
            return;
        }

        unsigned int blocknum = packet[2] * 256 + packet[3];

        if (blocknum == c->lastsent)
        {
            send_ack(c, true);
            c->stamp = time(0);
        }
        else if (blocknum == c->lastsent + 1)
        {
            fwrite(&packet[4], sizeof(char), packetsize-4, c->tftpfile);
            if (packetsize < c->blksize + 4)
            {
                log(WvLog::Debug, "File transferred successfully.\n");
                conns.remove(c);
            }
            send_ack(c);
        }
    }
}

// Send out the next packet, unless resend is true, in which case
// send out packets unack through lastsent.
void WvTFTPBase::send_data(TFTPConn *c, bool resend = false)
{
//    log("Sending data.\n");
    int firstpkt, lastpkt;

    if (resend)
    {
        firstpkt = c->unack;
        lastpkt = c->lastsent;
        fseek(c->tftpfile, (firstpkt - 1) * c->blksize, SEEK_SET);
    }
    else
    {
        c->lastsent++;
        if (c->lastsent == 65536)
        {
            c->lastsent = 0;
        }
        firstpkt = c->lastsent;
        lastpkt = c->lastsent;
    }

    for (int pktcount = firstpkt; pktcount <= lastpkt; pktcount++)
    {
        size_t datalen = 0;
        packetsize = 4;
    
        // DATA opcode
        packet[0] = 0;
        packet[1] = 3;
        // block num
        packet[2] = pktcount / 256;
        packet[3] = pktcount % 256;
        // data
        datalen = fread(&packet[4], sizeof(char), c->blksize, c->tftpfile);
        log(WvLog::Debug5, "Read %s bytes from file.\n", datalen);
        if (datalen < c->blksize)
            c->donefile = true;
        packetsize += datalen;
//        log(WvLog::Debug5, "Sending ");
        dump_pkt();
        write(packet, packetsize);
    }
}

// Send an acknowledgement.
void WvTFTPBase::send_ack(TFTPConn *c, bool resend = false)
{
    if (!resend)
    {
        if (++c->lastsent == 65536)
            c->lastsent = 0;
    }

    packetsize = 4;
    packet[0] = 0;
    packet[1] = 4;
    packet[2] = c->lastsent / 256;
    packet[3] = c->lastsent % 256;
//    log(WvLog::Debug5, "Sending ");
    dump_pkt();
    write(packet, packetsize); 
}

void WvTFTPBase::send_err(char errcode, WvString errmsg = "")
{
    packetsize = 4;

    packet[0] = 0;
    packet[1] = 5;
    packet[2] = 0;
    packet[3] = errcode;

    if (errmsg == "")
    {
        switch (errcode)
        {
            case 1: errmsg = "File not found.";
                    break;
            case 2: errmsg = "Access violation.";
                    break;
            case 3: errmsg = "Disk full or allocation exceeded.";
                    break;
            case 4: errmsg = "Illegal TFTP operation.";
                    break;
            case 5: errmsg = "Unknown transfer ID.";
                    break;
            case 6: errmsg = "File already exists.";
                    break;
            case 7: errmsg = "No such user.";
        }
    }
    strcpy(&packet[4],errmsg.edit());
    packetsize += errmsg.len() + 1;
//    log(WvLog::Debug5, "Sending Error ");
    dump_pkt();
    write(packet, packetsize);     
}

