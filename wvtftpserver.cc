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

#include "wvtftpserver.h"
#include "strutils.h"
#include "wvtimeutils.h"
#include <sys/stat.h>
#include <ctype.h>

WvTFTPServer::WvTFTPServer(UniConf &_cfg, int _tftp_tick)
    : WvTFTPBase(_tftp_tick, _cfg["TFTP"]["Port"].getmeint(69)), cfg(_cfg)
{
    log(WvLog::Info, "WvTFTP listening on %s.\n", *local());
}


WvTFTPServer::~WvTFTPServer()
{
    log(WvLog::Info, "WvTFTP shutting down.\n");
}


void WvTFTPServer::execute()
{
    WvTFTPBase::execute();

    check_timeouts();

    if (!conns.isempty())
        alarm(tftp_tick);

    packetsize = read(packet, MAX_PACKET_SIZE);
    if (packetsize > 0)
    {
        dump_pkt();
        if (!conns[remaddr])
            new_connection();
        else
	{
	    TFTPOpcode opcode = (TFTPOpcode)(packet[0] * 256 + packet[1]);
	    
	    if (opcode == RRQ)
	    {
		// last transaction was interrupted, and they're starting over,
		// I guess.
		conns.remove(conns[remaddr]);
		new_connection();
	    }
	    else
		handle_packet();
	}
    }
}


void WvTFTPServer::check_timeouts()
{
    time_t timeout;

    TFTPConnDict::Iter i(conns);
    for (i.rewind(); i.next(); )
    {
        int expect_packet = (i->direction == tftpwrite) ? i->lastsent :
            i->unack;
        timeout = cfg["TFTP"]["Min Timeout"].getmeint(100);
        if (!i->total_packets)
            timeout = 1000;
        if ((i->mult * i->mult * i->rtt / i->total_packets) > timeout)
            timeout = i->mult * i->mult * i->rtt / i->total_packets;
	
        struct timeval tv = wvtime();
        
        if (msecdiff(tv, *(i->pkttimes->get(expect_packet))) >= timeout)
        {
            i->numtimeouts++;

            log(WvLog::Debug1,
                "Timeout #%s (%s ms) on block %s from connection to %s.\n",
		i->numtimeouts, timeout, expect_packet, i->remote);
	    log(WvLog::Debug4, "[t1 %s, t2 %s, elapsed %s, expected %s]\n",
		tv.tv_sec, i->pkttimes->get(expect_packet)->tv_sec,
		msecdiff(tv, *(i->pkttimes->get(expect_packet))),
		expect_packet);
		
            log(WvLog::Debug4, "(packets %s, avg rtt %s, timeout %s, ms elapsed %s)\n",
                i->total_packets, 
                i->total_packets ? i->rtt / i->total_packets : 1000, timeout,
		msecdiff(tv, *(i->pkttimes->get(expect_packet))));
            if ((i->mult + 1) * (i->mult + 1) * i->rtt / i->total_packets <
		(time_t)cfg["TFTP"]["Max Timeout"].getmeint(5000))
                i->mult++;
            else
                log(WvLog::Debug1, "Max timeout reached.\n");
            log(WvLog::Debug4, "Increasing multiplier to %s.\n", i->mult * i->mult);

            // If the client times out too many times, stop sending it so much
            // at once - it could be choking on data.
            if ((i->numtimeouts % 5) == 0 && i->pktclump > 1) 
            {
                i->pktclump--;
                log(WvLog::Debug1, 
                        "Too many timeouts, reducing prefetch to %s\n", 
                        i->pktclump);
            }

            if (i->numtimeouts == cfg["TFTP/Max Timeout Count"].getmeint(1000))
            {
                log(WvLog::Info,"Max timeouts reached; aborting transfer.\n");
                send_err(0, "Too many timeouts.");
                conns.remove(&i());
            }
            else if (i->send_oack)
            {
                log(WvLog::Debug4, "Sending oack ");
                memcpy(packet, i->oack, 512);
                packetsize = i->oacklen;
                dump_pkt();
                write(packet, packetsize);
		i->timed_out_ignore = i->lastsent; 
            }
            else if (i->direction == tftpread)
	    {
                send_data(&i(), true);
		i->timed_out_ignore = i->lastsent; 
	    }
            else
	    {
                send_ack(&i(), true);
		i->timed_out_ignore = i->lastsent; 
	    }
	    
	    // 'i' might be invalid here!!
	    
	    // list might have lost an entry, screwing up the iterator;
	    // rewind it now.
	    i.rewind();
        }
    }
}


// Returns 0 if successful or the error number (1-8) if not.
int WvTFTPServer::validate_access(TFTPConn *c)
{
    WvString basedir = cfg["TFTP"]["Base dir"].getme("/tftpboot/");
    if (basedir[basedir.len() -1] != '/')
        basedir.append("/");

    if (strncmp(c->filename, basedir, basedir.len()))
        return 2;

    // As the original tftpd says, "prevent tricksters from getting
    // around the directory restrictions".
    if (!strncmp(c->filename, "../", 3))
        return 2;

    const char *cp;
    struct stat stbuf;
    for (cp = c->filename + 1; *cp; cp++)
        if(*cp == '.' && strncmp(cp-1, "/../", 4) == 0)
            return 2;
    if (stat(c->filename, &stbuf) < 0)
        if (c->direction == tftpread)
            return (errno == ENOENT ? 1 : 2);
        else
            return 0;            // If the file doesn't exist, allow writing.

    if (c->direction == tftpread)
    {
        if ((stbuf.st_mode&(S_IREAD >> 6)) == 0)
            return 2;
    }
    else
    {
        if ((stbuf.st_mode&(S_IWRITE >> 6)) == 0)
            return 2;
    }
    return 0; 
}


WvString WvTFTPServer::check_aliases(TFTPConn *c)
{
    if (!c)
	return WvString("");

    bool alias_once_fn_only = false;
    WvIPAddr clientportless = static_cast<WvIPAddr>(c->remote);

    WvString alias(cfg["TFTP Alias Once"][WvString("%s %s", clientportless,
						   c->filename)].getme(""));
    if (!alias)
    {
	alias = cfg["TFTP Alias Once"][c->filename].getme("");
	alias_once_fn_only = true;
    }

    if (!!alias)
    {
	log(WvLog::Debug4, "Alias once is \"%s\".\n", alias);
	c->alias_once = true;
	c->alias = cfg["TFTP Alias Once"][WvString("%s%s",
						   alias_once_fn_only ?
						   WvString("") :
						   WvString("%s ", clientportless),
						   c->filename)];
    }
    else
    {
	alias = cfg["TFTP Aliases"][WvString("%s %s", clientportless,
					     c->filename)].getme(cfg["TFTP Aliases"][c->filename].getme(""));
	log(WvLog::Debug4, "Alias is \"%s\".\n", alias);
    }

    return alias;
}


void WvTFTPServer::new_connection()
{
    log(WvLog::Info, "New connection from %s\n", remaddr);
    int code = packet[0]*256 + packet[1];
    log(WvLog::Debug4, "Packet opcode is %s.\n", code);
    if ((!code) || (code > 2))
    {
        log(WvLog::Debug, "Erroneous packet; aborting.\n");
        //send_err(4);
        return;
    }
    TFTPOpcode pktcode = static_cast<TFTPOpcode>(code);
    
    TFTPConn *c = new TFTPConn;
    c->remote = remaddr;
    WvIPAddr clientportless = static_cast<WvIPAddr>(c->remote);
    UniConfKey clientportlessk = UniConfKey(clientportless);

    if (!cfg["Registered TFTP Clients"][clientportlessk].getmeint(
             cfg["New TFTP Clients"][clientportlessk].getmeint(false)))
    {
        cfg["New TFTP Clients"][clientportlessk].setmeint(true);
    }

    // Make sure the filename and mode actually end with nulls.
    bool foundnull1 = false;
    bool foundnull2 = false;
    unsigned int modestart = 0;
    for (unsigned int i = 2; i < packetsize; i++)
    {
        if (packet[i] == 0)
        {
            if (foundnull1)
            {
                foundnull2 = true;
                break;
            }
            else
            {
                foundnull1 = true;
                modestart = i + 1;
            }
        }
    }
    if (!foundnull1 || !foundnull2)
    {
        log(WvLog::Debug, "Badly formed packet; aborting.\n");
        send_err(4);
        delete c;
        return;
    }

    WvString origfilename = &packet[2];
    c->filename = origfilename;

    c->direction = static_cast<TFTPDir>((int)pktcode-1);
    log(WvLog::Debug4, "Direction is %s.\n", c->direction);
    if ((c->direction == tftpwrite) && cfg["TFTP"]["Readonly"].getmeint(1))
    {
        log(WvLog::Warning, "Writes are not permitted.\n");
        send_err(2);
        delete c;
        return;
    }

    // convert mode to TFTPMode type.
    unsigned int ch;
    for (ch = modestart; packet[ch] != 0; ch++)
        packet[ch] = tolower(packet[ch]);
    if (!strcmp(&packet[modestart], "netascii"))
        c->mode = netascii;
    else if (!strcmp(&packet[modestart], "octet"))
        c->mode = octet;
    else if (!strcmp(&packet[modestart], "mail"))
        c->mode = mail;
    else
    { 
        log(WvLog::Info, "Unknown mode string \"%s\"; aborting.\n",
            &packet[modestart]);
        send_err(4);
        delete c;
        return;
    }
    log(WvLog::Debug4, "Mode is %s.\n", c->mode);

    c->blksize = 512;
    c->tsize = 0;
    c->pktclump = cfg["TFTP"]["Prefetch"].getmeint(3);
    c->pkttimes = new PktTime(c->pktclump);
    c->unack = 0;
    c->donefile = false;
    c->numtimeouts = 0;
    c->rtt = 1000;
    c->total_packets = 1;
    c->mult = 1;
    c->timed_out_ignore = -1;
    
    for (int i = 0; i < c->pktclump; i++)
    {
        struct timeval tv = wvtime();
	c->pkttimes->set(i, tv);
    }

    if (c->direction == tftpread)
        log(WvLog::Info, "Client is requesting to read '%s'.\n",
	    origfilename);
    else
        log(WvLog::Info, "Client is requesting to write '%s'.\n",
	    origfilename);

    if (!check_filename(c))
    {
        delete c;
        return;
    }

    if (origfilename != c->filename)
	log(WvLog::Info, "...using '%s' instead.\n", c->filename);

    if (c->direction == tftpread)
    {
        if (c->mode == netascii)
            c->tftpfile = fopen(c->filename, "r");
        else if (c->mode == octet)
            c->tftpfile = fopen(c->filename, "rb");

        if (!c->tftpfile)
        {
            log(WvLog::Info, "Failed to open file for reading; aborting.\n");
            send_err(2);
            delete c;
            return;
        }
    }
    else
    {
        if (c->mode == netascii)
            c->tftpfile = fopen(c->filename, "r");
        else if (c->mode == octet)
            c->tftpfile = fopen(c->filename, "rb");

        if (c->tftpfile)
        {
            log(WvLog::Info, "File already exists; aborting.\n");
            send_err(6);
            delete c;
            return;
        }

        umask(011);
        if (c->mode == netascii)
            c->tftpfile = fopen(c->filename, "w");
        else if (c->mode == octet)
            c->tftpfile = fopen(c->filename, "wb");

        if (!c->tftpfile)
        {
            log(WvLog::Info, "Failed to open file for writing; aborting.\n");
            send_err(3);
            delete c;
            return;
        }
    }

    c->send_oack = false;
    c->oacklen = 0;

    if (!process_options(c, ch+1))
    {
        delete c;
        return;
    }

    if (!strcmp(&packet[modestart], "netascii"))
      c->mode = netascii;
    else if (!strcmp(&packet[modestart], "octet"))
      c->mode = octet;
    else if (!strcmp(&packet[modestart], "mail"))
      c->mode = mail;
    else
    { 
	log(WvLog::Info, "Unknown mode string \"%s\"; aborting.\n",
	    &packet[modestart]);
	send_err(4);
	delete c;
	return;
    }

    alarm(tftp_tick);
    conns.add(c, true);
    if (c->direction == tftpread)
    {
        c->lastsent = 0;
        c->unack = 1;
        if (c->send_oack)
        {
            log(WvLog::Debug4, "Sending oack ");
            memcpy(packet, c->oack, 512);
            packetsize = c->oacklen;
            dump_pkt();
            write(packet, packetsize);
	    // Set pkttimes[1] to avoid timeouts on ACK for options.
	    struct timeval tv = wvtime();
	    c->pkttimes->set(1, tv);
        }
        else
        {
            log(WvLog::Debug4, "Last sent: %s unack: %s pktclump: %s\n",
                c->lastsent, c->unack, c->pktclump);
            int pktsremain = c->lastsent - c->unack;
            while (pktsremain < c->pktclump - 1)
            {
                log(WvLog::Debug4, "Result is %s\n", pktsremain);
                send_data(c);
                if (c->donefile)
                    break;
                pktsremain = c->lastsent - c->unack;
            }
        }
    }
    else
    {
        c->lastsent = -1;
        send_ack(c);
    }
}


bool WvTFTPServer::check_filename(TFTPConn *c)
{
    // Strip [TFTP]"Strip prefix" from filename, then compare it to 
    // [TFTP Aliases] entries.  If nothing is found, then add [TFTP]"Base dir"
    // and try again.

    WvString strip_prefix = cfg["TFTP"]["Strip prefix"].getme("");
    if (strip_prefix != "")
    {
        if (strip_prefix[strip_prefix.len() -1] != '/')
            strip_prefix.append("/");

        log(WvLog::Debug4, "Strip prefix is %s.\n", strip_prefix);
        if (!strncmp(c->filename, strip_prefix, strip_prefix.len()))
        {
            log(WvLog::Debug4, "Stripping prefix.\n");
            c->filename = WvString(&c->filename[strip_prefix.len()]);
        }
    }
    log(WvLog::Debug4, "Filename after stripping is %s.\n", c->filename);

    WvString alias(check_aliases(c));

    if (!!alias)
        c->filename = alias;

    WvString basedir = cfg["TFTP"]["Base dir"].getme("/tftpboot/");
    if (basedir[basedir.len() -1] != '/')
        basedir.append("/");

    // If the first char isn't /, add base dir and look for aliases
    // again.  If we don't need to add the base dir, we've already
    // looked for aliases above.
    if (c->filename[0] != '/')
    {
	c->filename = WvString("%s%s", basedir, c->filename);
        if (alias == "")
        {
            // Check for aliases again
            log(WvLog::Debug4,
		"Filename before 2nd alias check is %s.\n", c->filename);
	    alias = check_aliases(c);
	    if (!!alias)
		c->filename = alias;
            log(WvLog::Debug4,
		"Filename after adding basedir and checking for alias is %s.\n",
                c->filename);
        }
    }

    int tftpaccess = validate_access(c);
    if (tftpaccess == 1)
    {
        // File not found.  Check for default file.
        c->filename = cfg["TFTP"]["Default File"].getme("");
        if (!c->filename)
        {
            log(WvLog::Info, "File not found.  Aborting.\n");
            send_err(1);
	    return false;
        }
        alias = check_aliases(c);

        if (alias != "")
            c->filename = alias;
        if (c->filename[0] != '/')
            c->filename = WvString("%s%s", basedir, c->filename);

        tftpaccess = validate_access(c);
        if (tftpaccess)
        {
            log(WvLog::Warning, "File access failed (error %s).\n",
                tftpaccess);
            send_err(tftpaccess);
            return false;
        }
    }
    else if (tftpaccess)
    {
        log(WvLog::Warning, "File access failed (error %s).\n", tftpaccess);
        send_err(tftpaccess);
        return false;
    }
    log(WvLog::Debug4, "Filename is %s.\n", c->filename);
    return true;
}


unsigned int WvTFTPServer::process_options(TFTPConn *c, unsigned int opts_start)
{
    unsigned int p = opts_start;
    if (p < packetsize)
    {
        // One or more options available for reading.
        c->oack[0] = 0;
        c->oack[1] = 6;
        c->oacklen = 2;
        char *oackp = &(c->oack[2]);
        char *optname;
        char *optvalue = NULL; // Give optvalue a dummy value so gcc doesn't 
                               // complain about potential lack of
                               // initialization.
        while (p < packetsize-1)
        {
            optname = &packet[p];
            for (int i = 0; i < 2; i++)
            {
                while (packet[++p])
                {
                    if (p == packetsize)
                    {    
                        log(WvLog::Warning,
                            "Badly formed option %s.  Aborting.\n",
                            (i==0) ? "name" : "value");
                        send_err(8);
                        return 0;
                    }
                }
                if (!i)
                    optvalue = &packet[p+1];
            }
            p++;
            log(WvLog::Debug, "Option %s, value %s.\n", optname, optvalue);
            strlwr(optname);

            if (!strcmp(optname, "blksize"))
            {
                c->blksize = atoi(optvalue);
                if (c->blksize < 8 || c->blksize > 65464)
                {
                    WvString message("Request for blksize of %s is invalid.  "
                                     "Aborting.", c->blksize);
                    log(WvLog::Warning, "%s\n", message);
                    send_err(8, message);
                    return 0;
                }

                log(WvLog::Debug, "Blksize option enabled (%s octets).\n", c->blksize);
                strcpy(oackp, optname);
                oackp += strlen(optname) + 1;
                c->oacklen += strlen(optname) + 1;
                strcpy(oackp, optvalue);
                oackp += strlen(optvalue) + 1;
                c->oacklen += strlen(optvalue) + 1;
            }
            else if (!strcmp(optname, "timeout"))
                log(WvLog::Debug,
                    "Client request for timeout ignored.  Adaptive"
                    " retransmission is better.\n");
            else if (!strcmp(optname, "tsize"))
            {
                c->tsize = atoi(optvalue);
                if (c->tsize < 0)
                {
                    WvString message("Request for tsize of %s is invalid.  "
                                     "Aborting.", c->tsize);
                    log(WvLog::Warning, "%s\n", message);
                    send_err(8, message);
                    return 0;
                }

                if (c->tsize == 0 && c->direction == tftpread)
                {
                    struct stat tftpfilestat;
                    if (stat(c->filename, &tftpfilestat) != 0)
                    {
                        WvString message("Cannot get stats for file.  "
                                         "Aborting.");
                        log(WvLog::Warning, "%s\n", message);
                        send_err(8, message);
                        return 0;
                    }
                    c->tsize = tftpfilestat.st_size;
                }

                WvString oacktsize = WvString("%s", c->tsize);
                log(WvLog::Debug, "Tsize option enabled (%s octets).\n", c->tsize);
                strcpy(oackp, optname);
                oackp += strlen(optname) + 1;
                c->oacklen += strlen(optname) + 1;
                strcpy(oackp, oacktsize.edit());
                oackp += oacktsize.len() + 1;
                c->oacklen += oacktsize.len() + 1;
            }
        }
        if (c->oacklen)
            c->send_oack = true;
        else
            delete c->oack;
    }

    return p;
}
