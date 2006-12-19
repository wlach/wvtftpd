/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2004 Net Integration Technologies, Inc.
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

#include "wvtftpbase.h"
#include "wvstrutils.h"
#include "wvtimeutils.h"
#include <assert.h>

PktTime::PktTime(int _pktclump)
{
    pktclump = _pktclump;
    times = new struct timeval[pktclump];
    idx = 0;
}

PktTime::~PktTime()
{
    deletev times;
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
    times[pktnum - idx].tv_sec = tv.tv_sec;
    times[pktnum - idx].tv_usec = tv.tv_usec;
}

struct timeval *PktTime::get(int pktnum)
{
    if (pktnum < idx || pktnum > (idx + pktclump))
        return NULL;
    return &times[pktnum - idx];
}

WvTFTPBase::WvTFTPBase(int _tftp_tick, int port)
    : WvUDPStream(port, WvIPPortAddr()), conns(5), log("WvTFTP", WvLog::Debug),
      tftp_tick(_tftp_tick)
{
}

WvTFTPBase::~WvTFTPBase()
{
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
    c->last_received = wvtime();
    TFTPOpcode opcode = (TFTPOpcode)(packet[0] * 256 + packet[1]);

    if (opcode == ERROR)
    {
        log(WvLog::Warning, "Received error packet; aborting.\n");
        conns.remove(c);
        return;
    }

    if (c->direction == tftpread)
    {
        // Packet should be an ack.
        if (opcode != ACK)
        {
            log(WvLog::Warning, "Expected ACK (read); aborting.\n");
            send_err(4);
            conns.remove(c);
            return;
        }

        c->mult = 1;
        int small_blocknum = (unsigned char)(packet[2]) * 256 +
	    	             (unsigned char)(packet[3]);
        int mult = c->unack / 65536;
        int blocknum = mult * 65536 + small_blocknum;
        if (blocknum > c->unack + 32000)
            blocknum = (mult - 1) * 65536 + small_blocknum;
        log(WvLog::Debug5,
	    "Handle: ack blocknum=%s(%s), unack=%s, lastsent=%s, "
            "prefetch=%s\n",
            small_blocknum, blocknum, c->unack, c->lastsent, c->pktclump);
	
        if (blocknum == 0 && c->send_oack)
        {
	    // treat the first block specially if we need to send an option
	    // acknowledgement.
            c->send_oack = false;
            log(WvLog::Debug5, "Last sent: %s unack: %s pktclump: %s\n",
                c->lastsent, c->unack, c->pktclump);
            int pktsremain = c->lastsent - c->unack;
            while (pktsremain < c->pktclump - 1)
            {
                log(WvLog::Debug5, "Result is %s\n", pktsremain);
                log(WvLog::Debug5, "Send\n");
                send_data(c);
                if (c->donefile)
                    break;
                pktsremain = c->lastsent - c->unack;
            }
	    
	    c->numtimeouts = 0;
        }
        else if (blocknum != (c->unack - 1)) // ignore duplicate ACK
        {
            // Add rtt to cumulative sum.
            if (blocknum == c->unack && blocknum > c->timed_out_ignore)
            {
                struct timeval tv = wvtime();
		
                time_t rtt = msecdiff(tv, *(c->pkttimes->get(blocknum)));
                log(WvLog::Debug4, "rtt is %s.\n", rtt);
		
                c->rtt += rtt;
                c->total_packets++;
            }
	    
            if (blocknum == c->unack && blocknum == c->lastsent
                && c->donefile)
            {
                // transfer completed if we haven't sent any packets last
                // time we acked, and this is the right ack.
                log(WvLog::Info, "File transferred successfully.\n");
                log(WvLog::Info, "Average rtt was %s ms.\n", c->rtt /
		    blocknum);

		if (c->alias_once)
		    c->alias.remove();

		conns.remove(c);
		c = NULL;
	    }
            else
            {
                // send the next packet if the ack comes from one of the
                // sent data packets.
                int blocknum_norollover = blocknum +
                                          (blocknum < c->unack ? 65536 : 0);
                if (c->unack <= blocknum_norollover &&
                    blocknum_norollover < (c->unack + c->pktclump))
                {
                    c->unack = blocknum + 1;

                    while ((c->lastsent - c->unack) < c->pktclump - 1)
                    {
                        if (c->donefile)
                            break;
                        send_data(c);
                        c->numtimeouts = 0;
                    }
                }
                else
                    log(WvLog::Error, "Received unexpected ACK for block "
                        "number %s.\n", blocknum);
            }
        }
	
	// 'c' might be invalid here if it was deleted!
    }
    else
    {
        // Packet should be data.
        if (opcode != DATA)
        {
            log(WvLog::Warning, "Badly formed packet (write); aborting.\n");
            send_err(4);
            conns.remove(c);
            return;
        }

        c->mult = 1;
        int small_blocknum = (unsigned char)(packet[2]) * 256 +
	    	             (unsigned char)(packet[3]);
        int mult = c->lastsent / 65536;
        int blocknum = mult * 65536 + small_blocknum;
        if (blocknum < c->lastsent - 32000)
            blocknum = (mult + 1) * 65536 + small_blocknum;

        if (blocknum == c->lastsent + 1)
        {
            unsigned int data_packetsize = packetsize;
            fwrite(&packet[4], sizeof(char), data_packetsize-4, c->tftpfile);

            // Add rtt to cumulative sum.
            if (blocknum > c->timed_out_ignore)
            {
                struct timeval tv = wvtime();
                time_t rtt = msecdiff(tv, *(c->pkttimes->get(blocknum - 1)));
                log("rtt is %s.\n", rtt);

                c->rtt += rtt;
                c->total_packets++;
            }
            send_ack(c);

            if (data_packetsize < c->blksize + 4)
            {
                log(WvLog::Info, "File transferred successfully.\n");
                log(WvLog::Info, "Average rtt was %s ms.\n", c->rtt /
		    blocknum);
		conns.remove(c);
		c = NULL;
            }
        }
    }
}

// Send out the next packet, unless resend is true, in which case
// send out packets unack through lastsent.
void WvTFTPBase::send_data(TFTPConn *c, bool resend)
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
        firstpkt = c->lastsent;
        lastpkt = c->lastsent;
    }
    
    log(WvLog::Debug5, "send_data: sending packets %s->%s\n",
	firstpkt, lastpkt);

    for (int pktcount = firstpkt; pktcount <= lastpkt; pktcount++)
    {
        size_t datalen = 0;
        packetsize = 4;
    
        // DATA opcode
        packet[0] = 0;
        packet[1] = 3;
        // block num
        packet[2] = (pktcount % 65536) / 256;
        packet[3] = (pktcount % 65536) % 256;
        // data
        datalen = fread(&packet[4], sizeof(char), c->blksize, c->tftpfile);
        log(WvLog::Debug5, "send_data: read %s bytes from file.\n", datalen);
        if (datalen < c->blksize)
            c->donefile = true;
        packetsize += datalen;
//        log(WvLog::Debug5, "Sending ");
        dump_pkt();
        setdest(c->remote);  // often redundant (after reading), but safer
        write(packet, packetsize);

        struct timeval tv = wvtime();
        c->pkttimes->set(pktcount, tv);
    }
}

// Send an acknowledgement.
void WvTFTPBase::send_ack(TFTPConn *c, bool resend)
{
    if (!resend)
        c->lastsent++;

    packetsize = 4;
    packet[0] = 0;
    packet[1] = 4;
    packet[2] = (c->lastsent % 65536) / 256;
    packet[3] = (c->lastsent % 65536) % 256;
//    log(WvLog::Debug5, "Sending ");
    dump_pkt();
    write(packet, packetsize); 

    struct timeval tv = wvtime();
    log(WvLog::Debug4, "Setting %s\n", c->lastsent);
    c->pkttimes->set(c->lastsent, tv);
}

void WvTFTPBase::send_err(char errcode, WvString errmsg)
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

