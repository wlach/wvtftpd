/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 */
#include "wvtftpserver.h"
#include "strutils.h"
#include "wvtimeutils.h"
#include <sys/stat.h>
#include <ctype.h>

WvTFTPServer::WvTFTPServer(WvConf &_cfg, int _tftp_tick)
    : WvTFTPBase(_tftp_tick, _cfg.getint("TFTP", "Port", 69)), cfg(_cfg)
{
    log(WvLog::Info, "WvTFTP listening on %s.\n", *local());
}

WvTFTPServer::~WvTFTPServer()
{
    log(WvLog::Info, "WvTFTP shutting down.\n");
}

void WvTFTPServer::execute()
{
    int connscount = 0;
    time_t timeout;
    
    WvTFTPBase::execute();
    
    TFTPConnDict::Iter i(conns);
    for (i.rewind(); i.next(); )
    {
        int expect_packet = (i->direction == tftpwrite) ? i->lastsent :
            i->unack;
        connscount++;
        timeout = cfg.getint("TFTP", "Min Timeout", 100);
        if (!i->total_packets)
            timeout = 1000;
        if ((i->mult * i->mult * i->rtt / i->total_packets) > timeout)
            timeout = i->mult * i->mult * i->rtt / i->total_packets;
	
        struct timeval tv = wvtime();
        
        if (msecdiff(tv, *(i->pkttimes->get(expect_packet))) >= timeout)
        {
            log(WvLog::Debug1,
                "Timeout (%s ms) on block %s from connection to %s.\n", 
		timeout, expect_packet, i->remote);
	    log(WvLog::Debug2, "[t1 %s, t2 %s, elapsed %s, expected %s]\n",
		tv.tv_sec, i->pkttimes->get(expect_packet)->tv_sec,
		msecdiff(tv, *(i->pkttimes->get(expect_packet))),
		expect_packet);
		
            log("(packets %s, avg rtt %s, timeout %s, ms elapsed %s)\n",
                i->total_packets, 
                i->total_packets ? i->rtt / i->total_packets : 1000, timeout,
		msecdiff(tv, *(i->pkttimes->get(expect_packet))));
            if (i->mult < cfg.getint("TFTP", "Max Mult", 20))
                i->mult++;
            log("Increasing multiplier to %s.\n", i->mult * i->mult);

            if (++i->numtimeouts == max_timeouts)
            {
                log(WvLog::Debug,"Max timeouts reached; aborting transfer.\n");
                send_err(0, "Too many timeouts.");
                conns.remove(&i());
            }
            else if (i->send_oack)
            {
                log(WvLog::Debug5, "Sending oack ");
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

    if (connscount)
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

// Returns 0 if successful or the error number (1-8) if not.
int WvTFTPServer::validate_access(TFTPConn *c, WvString &basedir)
{
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

void WvTFTPServer::new_connection()
{
    log(WvLog::Debug, "New connection from %s\n", remaddr);
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

    if (!cfg.getint("Registered TFTP Clients", clientportless,
             cfg.getint("New TFTP Clients", clientportless, false)))
    {
        cfg.setint("New TFTP Clients", clientportless, true);
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
    if ((c->direction == tftpwrite) && cfg.getint("TFTP", "Readonly", 1))
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
        log(WvLog::Debug, "Unknown mode string \"%s\"; aborting.\n",
            &packet[modestart]);
        send_err(4);
        delete c;
        return;
    }
    log(WvLog::Debug4, "Mode is %s.\n", c->mode);

    c->blksize = 512;
    c->tsize = 0;
    c->pktclump = cfg.getint("TFTP", "Prefetch", 3);
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

    // Strip [TFTP]"Strip prefix" from filename, then compare it to 
    // [TFTP Aliases] entries.  If nothing is found, then add [TFTP]"Base dir"
    // and try again.

    WvString strip_prefix = cfg.get("TFTP", "Strip prefix", "");
    if (strip_prefix != "")
    {
        if (strip_prefix[strip_prefix.len() -1] != '/')
            strip_prefix.append("/");

        log(WvLog::Debug5, "strip prefix is %s.\n", strip_prefix);
        if (!strncmp(c->filename, strip_prefix, strip_prefix.len()))
        {
            log(WvLog::Debug5, "Stripping prefix.\n");
            c->filename = WvString(&c->filename[strip_prefix.len()]);
        }
    }
    log(WvLog::Debug5, "Filename after stripping is %s.\n", c->filename);
    WvString alias = cfg.get("TFTP Aliases", WvString("%s %s", clientportless,
            c->filename), cfg.get("TFTP Aliases", c->filename,
            ""));
    log(WvLog::Debug5, "Alias is %s.\n", alias);
    if (alias != "")
        c->filename = alias;

    WvString basedir = cfg.get("TFTP", "Base dir", "/tftpboot/");
    if (basedir[basedir.len() -1] != '/')
        basedir.append("/");

    // If the first char isn't /, add base dir and look for aliases
    // again.  If we don't need to add the base dir, we've already
    // looked for aliases above.
    if (c->filename[0] != '/')
    {
	WvString newname("%s%s", basedir, c->filename);
        c->filename = newname;
        if (alias == "")
        {
            // Check for aliases again
            log(WvLog::Debug5,
		"Filename before 2nd alias check is %s.\n", c->filename);
            WvString newname2(
		   cfg.get("TFTP Aliases", 
			   WvString("%s %s", clientportless, c->filename),
			   cfg.get("TFTP Aliases", c->filename, c->filename))
		   );
	    c->filename = newname2;
	    log(c->filename);
            log(WvLog::Debug5,
		"Filename after adding basedir and checking for alias is %s.\n",
                c->filename);
        }
    }
  
    if (c->direction == tftpread)
        log(WvLog::Debug, "Client is requesting to read '%s'.\n",
	    origfilename);
    else
        log(WvLog::Debug, "Client is requesting to write '%s'.\n",
	    origfilename);
    if (origfilename != c->filename)
	log(WvLog::Debug, "...using '%s' instead.\n", c->filename);

    int tftpaccess = validate_access(c, basedir);
    if (tftpaccess)
    {
        if (tftpaccess == 1)
        {
            // File not found.  Check for default file.
            c->filename = cfg.get("TFTP", "Default File", "");
            if (c->filename == "")
            {
                log(WvLog::Debug, "File not found.  Aborting.\n");
                send_err(1);
                delete c;
                return; 
            }
            alias = cfg.get("TFTP Aliases", WvString("%s %s", clientportless,
                c->filename), cfg.get("TFTP Aliases", c->filename,
                ""));
            if (alias != "")
                c->filename = alias;
            if (c->filename[0] != '/')
            {
                WvString newname = basedir;
                newname.append(c->filename);
                c->filename = newname;
            }
            tftpaccess = validate_access(c, basedir);
            if (tftpaccess)
            {
                log(WvLog::Debug, "File access failed (error %s).\n",
                    tftpaccess);
                send_err(tftpaccess);
                delete c;
                return;
            }
        }
        else
        {
            log(WvLog::Debug, "File access failed (error %s).\n", tftpaccess);
            send_err(tftpaccess);
            delete c;
            return;
        }
    }
    log(WvLog::Debug4, "Filename is %s.\n", c->filename);

    if (c->direction == tftpread)
    {
        if (c->mode == netascii)
            c->tftpfile = fopen(c->filename, "r");
        else if (c->mode == octet)
            c->tftpfile = fopen(c->filename, "rb");

        if (!c->tftpfile)
        {
            log(WvLog::Debug, "Failed to open file for reading; aborting.\n");
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
            log(WvLog::Debug, "File already exists; aborting.\n");
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
            log(WvLog::Debug, "Failed to open file for writing; aborting.\n");
            send_err(3);
            delete c;
            return;
        }
    }

    c->send_oack = false;
    c->oacklen = 0;
    // Look for options.
    ch++;
    if (ch < packetsize)
    {
        // One or more options available for reading.
        c->oack[0] = 0;
        c->oack[1] = 6;
        c->oacklen = 2;
        char * oackp = &(c->oack[2]);
        char * optname;
        char * optvalue = NULL; // Give optvalue a dummy value so gcc doesn't 
                                // complain about potential lack of
                                // initialization.
        while (ch < packetsize-1)
        {
            optname = &packet[ch];
            for (int i = 0; i < 2; i++)
            {
                while (packet[++ch])
                {
                    if (ch == packetsize)
                    {    
                        log(WvLog::Debug,
                            "Badly formed option %s.  Aborting.\n",
                            (i==0) ? "name" : "value");
                        send_err(8);
                        delete c;
                        return;
                    }
                }
                if (!i)
                    optvalue = &packet[ch+1];
            }
            ch++;
            log(WvLog::Debug4, "Option %s, value %s.\n", optname, optvalue);
            strlwr(optname);

            if (!strcmp(optname, "blksize"))
            {
                c->blksize = atoi(optvalue);
                if (c->blksize < 8 || c->blksize > 65464)
                {
                    WvString message = WvString(
                        "Request for blksize of %s is invalid.  Aborting.",
                        c->blksize);
                    log(WvLog::Debug, "%s\n", message);
                    send_err(8, message);
                    delete c;
                    return;
                }
                else
                {
                    log("blksize option enabled (%s octets).\n",
                        c->blksize);
                    strcpy(oackp, optname);
                    oackp += strlen(optname) + 1;
                    c->oacklen += strlen(optname) + 1;
                    strcpy(oackp, optvalue);
                    oackp += strlen(optvalue) + 1;
                    c->oacklen += strlen(optvalue) + 1;
                }
            }
            else if (!strcmp(optname, "timeout"))
                log(WvLog::Debug4,
                    "Client request for timeout ignored.  Adaptive"
                    " retransmission is better.\n");
            else if (!strcmp(optname, "tsize"))
            {
                c->tsize = atoi(optvalue);
                if (c->tsize < 0)
                {
                    WvString message = WvString(
                        "Request for tsize of %s is invalid.  Aborting.",
                        c->tsize);
                    log(WvLog::Debug, "%s\n", message);
                    send_err(8, message);
                    delete c;
                    return;
                }
                else
                {
                    if (c->tsize == 0 && c->direction == tftpread)
                    {
                        struct stat tftpfilestat;
                        if (stat(c->filename, &tftpfilestat) != 0)
                        {
                            WvString message = 
                                "Cannot get stats for file.  Aborting.";
                           log(WvLog::Debug, "%s\n", message);
                           send_err(8, message);
                           delete c;
                           return;
                        }
                        c->tsize = tftpfilestat.st_size;
                    }
                    WvString oacktsize = WvString("%s", c->tsize);
                    log("tsize option enabled (%s octets).\n",
                        c->tsize);
                    strcpy(oackp, optname);
                    oackp += strlen(optname) + 1;
                    c->oacklen += strlen(optname) + 1;
                    strcpy(oackp, oacktsize.edit());
                    oackp += oacktsize.len() + 1;
                    c->oacklen += oacktsize.len() + 1;
                }
            }
        }
        if (c->oacklen)
            c->send_oack = true;
        else
            delete c->oack;
        if (!strcmp(&packet[modestart], "netascii"))
            c->mode = netascii;
        else if (!strcmp(&packet[modestart], "octet"))
           c->mode = octet;
        else if (!strcmp(&packet[modestart], "mail"))
           c->mode = mail;
        else
        { 
            log(WvLog::Debug, "Unknown mode string \"%s\"; aborting.\n",
                &packet[modestart]);
            send_err(4);
            delete c;
            return;
        }
    }

    alarm(tftp_tick);
    conns.add(c, true);
    if (c->direction == tftpread)
    {
        c->lastsent = 0;
        c->unack = 1;
        if (c->send_oack)
        {
            log(WvLog::Debug5, "Sending oack ");
            memcpy(packet, c->oack, 512);
            packetsize = c->oacklen;
            dump_pkt();
            write(packet, packetsize);
        }
        else
        {
            log(WvLog::Debug5, "last sent: %s unack: %s pktclump: %s\n",
                c->lastsent, c->unack, c->pktclump);
            int pktsremain = c->lastsent - c->unack;
            while (pktsremain < c->pktclump - 1)
            {
                log(WvLog::Debug5, "result is %s\n", pktsremain);
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

