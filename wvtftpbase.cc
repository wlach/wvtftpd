/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 */
#include "wvtftpbase.h"
#include "strutils.h"
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

WvTFTPBase::WvTFTPBase(int _tftp_tick, int port = 0)
    : WvUDPStream(port, WvIPPortAddr()), conns(5), log("WvTFTP", WvLog::Debug4),
      tftp_tick(_tftp_tick), max_timeouts(25)
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
    TFTPOpcode opcode = (TFTPOpcode)(packet[0] * 256 + packet[1]);

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
            log(WvLog::Debug, "Expected ACK (read); aborting.\n");
            send_err(4);
            conns.remove(c);
            return;
        }

        int blocknum = (unsigned char)(packet[2]) * 256 +
	    	       (unsigned char)(packet[3]);
        log(WvLog::Debug5,
	    "handle: got blocknum %s; unack is %s; lastsent is %s.\n",
            blocknum, c->unack, c->lastsent);
	
        if (blocknum == 0 && c->send_oack)
        {
	    // treat the first block specially if we need to send an option
	    // acknowledgement.
            c->send_oack = false;
            log(WvLog::Debug5, "last sent: %s unack: %s pktclump: %s\n",
                c->lastsent, c->unack, c->pktclump);
            int pktsremain = c->lastsent - c->unack;
            while (pktsremain < c->pktclump - 1)
            {
                log(WvLog::Debug5, "result is %s\n", pktsremain);
                log(WvLog::Debug5, "send\n");
                send_data(c);
                if (c->donefile)
                    break;
                pktsremain = c->lastsent - c->unack;
            }
	    
	    c->numtimeouts = 0;
        }
        else if (blocknum != ((c->unack - 1) % 65536)) // ignore duplicate ACK
        {
            // Add rtt to cumulative sum.
            if (blocknum == c->unack && blocknum > c->timed_out_ignore)
            {
		struct timeval tv = wvtime();
		
		time_t rtt = msecdiff(tv,
		      c->last_pkt_time[(blocknum - c->lpt_idx) % 65536]);
		log("rtt is %s.\n", rtt);
		
		c->rtt += rtt;
		c->total_packets++;
            }
	    
	    if (blocknum == c->unack && blocknum == c->lastsent
		&& c->donefile)
	    {
		// transfer completed if we haven't sent any packets last
		// time we acked, and this is the right ack.
		log(WvLog::Debug, "File transferred successfully.\n");
		log(WvLog::Debug, "Average rtt was %s ms.\n", c->rtt /
		    blocknum);
		conns.remove(c);
		c = NULL;
	    }
	    else if (blocknum == c->unack)
	    {
		// send the next packet if the first unacked packet is the
		// one being acked.
		c->unack = (blocknum + 1) % 65536;
		
		while (((c->lastsent - c->unack) % 65536) < c->pktclump - 1)
		{
		    if (c->donefile)
			break;
		    send_data(c);
		    c->numtimeouts = 0;
		}
	    }
        }
	
	// 'c' might be invalid here if it was deleted!
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

        int blocknum = packet[2] * 256 + packet[3];

        if (blocknum == c->lastsent)
            send_ack(c, true);
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
	c->lastsent %= 65536;
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
        packet[2] = pktcount / 256;
        packet[3] = pktcount % 256;
        // data
        datalen = fread(&packet[4], sizeof(char), c->blksize, c->tftpfile);
        log(WvLog::Debug5, "send_data: read %s bytes from file.\n", datalen);
        if (datalen < c->blksize)
            c->donefile = true;
        packetsize += datalen;
//        log(WvLog::Debug5, "Sending ");
        dump_pkt();
        write(packet, packetsize);

        struct timeval tv = wvtime();
	
	// if this packet is off the end of last_pkt_time, slide the
	// last_packet_time window by adjusting lpt_idx.
        if (c->lpt_idx + c->pktclump <= pktcount)
        {
            int d = pktcount - (c->lpt_idx + c->pktclump) + 1;
            c->lpt_idx += d;
	    
	    assert(c->lpt_idx <= c->unack);
	    assert(c->lpt_idx + c->pktclump > c->unack);
	    
	    if (d >= c->pktclump)
	    {
		// wipe them all
                memset(c->last_pkt_time, 0,
		       c->pktclump * sizeof(*c->last_pkt_time));
	    }
            else if (d > 0)
            {
		// slide the window as much as necessary
                memmove(c->last_pkt_time, c->last_pkt_time + d, 
			(c->pktclump - d) * sizeof(*c->last_pkt_time));
                memset(c->last_pkt_time + c->pktclump - d, 0,
		       d * sizeof(*c->last_pkt_time));
            }
        }
	
        c->last_pkt_time[(pktcount - c->lpt_idx) % 65536] = tv;
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

