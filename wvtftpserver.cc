/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2002 Net Integration Technologies, Inc.
 */
#include <ctype.h>
#include "wvtftpserver.h"
#include "strutils.h"
#include <sys/stat.h>

WvTFTPServer::WvTFTPServer(WvConf &_cfg, int _tftp_tick, int def_timeout)
    : WvTFTPBase(_tftp_tick, def_timeout, 69), cfg(_cfg)
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
    
    int connscount = 0;
    WvTFTPServer::TFTPConnDict::Iter i(conns);
    for (i.rewind(); i.next(); )
    {
        connscount++;
        if (difftime(time(0), i().stamp) >= i().timeout)
        {
            log("Timeout on connection from %s.\n", i().remote);
            if (++i().numtimeouts == max_timeouts)
            {
                log(WvLog::Debug,"Max timeouts reached; aborting transfer.\n");
                send_err(0, "Too many timeouts.");
                conns.remove(&i());
            }
            else if (i().send_oack)
            {
                log(WvLog::Debug5, "Sending oack ");
                memcpy(packet, i().oack, 512);
                packetsize = i().oacklen;
                dump_pkt();
                write(packet, packetsize);
            }
            else if (i().direction == tftpread)
                send_data(&i(), true);
            else
                send_ack(&i(), true);
	    
	    // list might have lost an entry, screwing up the iterator;
	    // rewind now.
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
        return (errno == ENOENT ? 1 : 2);
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
        send_err(4);
        return;
    }
    TFTPOpcode pktcode = static_cast<TFTPOpcode>(code);
    
    TFTPConn *newconn = new TFTPConn;
    newconn->remote = remaddr;
    WvIPAddr clientportless = static_cast<WvIPAddr>(newconn->remote);

    if (!cfg.getint("Registered TFTP Clients", clientportless,
             cfg.getint("New TFTP Clients", clientportless, false)))
    {
        cfg.setint("New TFTP Clients", clientportless, true);
        cfg.flush();
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
        delete newconn;
        return;
    }

    newconn->filename = &packet[2];

    newconn->direction = static_cast<TFTPDir>(static_cast<int>(pktcode)-1);
    log(WvLog::Debug4, "Direction is %s.\n", newconn->direction);

    // convert mode to TFTPMode type.
    unsigned int ch;
    for (ch = modestart; packet[ch] != 0; ch++)
        packet[ch] = tolower(packet[ch]);
    if (!strcmp(&packet[modestart], "netascii"))
        newconn->mode = netascii;
    else if (!strcmp(&packet[modestart], "octet"))
        newconn->mode = octet;
    else if (!strcmp(&packet[modestart], "mail"))
        newconn->mode = mail;
    else
    { 
        log(WvLog::Debug, "Unknown mode string \"%s\"; aborting.\n",
            &packet[modestart]);
        send_err(4);
        delete newconn;
        return;
    }
    log(WvLog::Debug4, "Mode is %s.\n", newconn->mode);

    newconn->timeout = def_timeout;
    newconn->blksize = 512;
    newconn->tsize = 0;
    newconn->pktclump = cfg.getint("TFTP", "Prefetch", 3);
    newconn->unack = 0;
    newconn->donefile = false;
    newconn->numtimeouts = 0;

    // Strip [TFTP]"Strip prefix" from filename, then compare it to 
    // [TFTP Aliases] entries.  If nothing is found, then add [TFTP]"Base dir"
    // and try again.

    WvString strip_prefix = cfg.get("TFTP", "Strip prefix", "");
    if (strip_prefix != "")
    {
        if (strip_prefix[strip_prefix.len() -1] != '/')
            strip_prefix.append("/");

        log(WvLog::Debug5, "strip prefix is %s.\n", strip_prefix);
        if (!strncmp(newconn->filename, strip_prefix, strip_prefix.len()))
        {
            log(WvLog::Debug5, "Stripping prefix.\n");
            newconn->filename = WvString(&newconn->filename[strip_prefix.len()]);
        }
    }
    log(WvLog::Debug5, "Filename after stripping is %s.\n", newconn->filename);
    WvString alias = cfg.get("TFTP Aliases", WvString("%s %s", clientportless,
            newconn->filename), cfg.get("TFTP Aliases", newconn->filename,
            ""));
    log(WvLog::Debug5, "Alias is %s.\n", alias);
    if (alias != "")
        newconn->filename = alias;

    WvString basedir = cfg.get("TFTP", "Base dir", "/tftpboot/");
    if (basedir[basedir.len() -1] != '/')
        basedir.append("/");

    // If the first char isn't /, add base dir and look for aliases
    // again.  If we don't need to add the base dir, we've already
    // looked for aliases above.
    if (newconn->filename[0] != '/')
    {
        WvString newname = basedir;
        newname.append(newconn->filename);
        newconn->filename = newname;
        if (alias == "")
        {
            // Check for aliases again
            log(WvLog::Debug5, "Filename before 2nd alias check is %s.\n", newconn->filename);
            WvString newname = cfg.get("TFTP Aliases", WvString("%s %s",
                clientportless, newconn->filename), cfg.get("TFTP Aliases",
                newconn->filename, newconn->filename));
            newconn->filename = newname;
            log(WvLog::Debug5, "Filename after adding basedir and checking for alias is %s.\n",
                newconn->filename);
        }
        newconn->filename.unique();
    }
  
    if (newconn->direction == tftpread)
        log(WvLog::Debug, "Client is requesting to read %s.\n", newconn->filename);
    else
        log(WvLog::Debug, "Client is requesting to write%s.\n", newconn->filename);

    int tftpaccess = validate_access(newconn, basedir);
    if (tftpaccess)
    {
        if (tftpaccess == 1)
        {
            // File not found.  Check for default file.
            newconn->filename = cfg.get("TFTP", "Default File", "");
            if (newconn->filename == "")
            {
                log(WvLog::Debug, "File not found.  Aborting.\n");
                send_err(1);
                delete newconn;
                return; 
            }
            alias = cfg.get("TFTP Aliases", WvString("%s %s", clientportless,
                newconn->filename), cfg.get("TFTP Aliases", newconn->filename,
                ""));
            if (alias != "")
                newconn->filename = alias;
            if (newconn->filename[0] != '/')
            {
                WvString newname = basedir;
                newname.append(newconn->filename);
                newconn->filename = newname;
                newconn->filename.unique();
            }
            tftpaccess = validate_access(newconn, basedir);
            if (tftpaccess)
            {
                log(WvLog::Debug, "File access failed (error %s).\n", tftpaccess);
                send_err(tftpaccess);
                delete newconn;
                return;
            }
        }
        else
        {
            log(WvLog::Debug, "File access failed (error %s).\n", tftpaccess);
            send_err(tftpaccess);
            delete newconn;
            return;
        }
    }
    log(WvLog::Debug4, "Filename is %s.\n", newconn->filename);

    if (newconn->direction == tftpread)
    {
        if (newconn->mode == netascii)
            newconn->tftpfile = fopen(newconn->filename, "r");
        else if (newconn->mode == octet)
            newconn->tftpfile = fopen(newconn->filename, "rb");

        if (!newconn->tftpfile)
        {
            log(WvLog::Debug, "Failed to open file for reading; aborting.\n");
            send_err(2);
            delete newconn;
            return;
        }
    }
    else
    {
        if (newconn->mode == netascii)
            newconn->tftpfile = fopen(newconn->filename, "r");
        else if (newconn->mode == octet)
            newconn->tftpfile = fopen(newconn->filename, "rb");

        if (newconn->tftpfile)
        {
            log(WvLog::Debug, "File already exists; aborting.\n");
            send_err(6);
            delete newconn;
            return;
        }

        if (newconn->mode == netascii)
            newconn->tftpfile = fopen(newconn->filename, "w");
        else if (newconn->mode == octet)
            newconn->tftpfile = fopen(newconn->filename, "wb");

        if (!newconn->tftpfile)
        {
            log(WvLog::Debug, "Failed to open file for writing; aborting.\n");
            send_err(3);
            delete newconn;
            return;
        }
    }

    newconn->send_oack = false;
    newconn->oacklen = 0;
    // Look for options.
    ch++;
    if (ch < packetsize)
    {
        // One or more options available for reading.
        newconn->oack[0] = 0;
        newconn->oack[1] = 6;
        newconn->oacklen = 2;
        char * oackp = &(newconn->oack[2]);
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
                        delete newconn;
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
                newconn->blksize = atoi(optvalue);
                if (newconn->blksize < 8 || newconn->blksize > 65464)
                {
                    WvString message = WvString(
                        "Request for blksize of %s is invalid.  Aborting.",
                        newconn->blksize);
                    log(WvLog::Debug, "%s\n", message);
                    send_err(8, message);
                    delete newconn;
                    return;
                }
                else
                {
                    log("blksize option enabled (%s octets).\n",
                        newconn->blksize);
                    strcpy(oackp, optname);
                    oackp += strlen(optname) + 1;
                    newconn->oacklen += strlen(optname) + 1;
                    strcpy(oackp, optvalue);
                    oackp += strlen(optvalue) + 1;
                    newconn->oacklen += strlen(optvalue) + 1;
                }
            }
            else if (!strcmp(optname, "timeout"))
            {
                int newtimeout = atoi(optvalue);
                if (newtimeout*1000 < tftp_tick || newtimeout > 255)
                {
                    log(WvLog::Debug4,
                        "Request for timeout of %s is invalid.  Ignoring.",
                        newtimeout);
                }
                else
                {
                    newconn->timeout = newtimeout;
                    log("timeout option enabled (%s seconds).\n",
                        newconn->timeout);
                    strcpy(oackp, optname);
                    oackp += strlen(optname) + 1;
                    newconn->oacklen += strlen(optname) + 1;
                    strcpy(oackp, optvalue);
                    oackp += strlen(optvalue) + 1;
                    newconn->oacklen += strlen(optvalue) + 1;
                }
            }
            else if (!strcmp(optname, "tsize"))
            {
                newconn->tsize = atoi(optvalue);
                if (newconn->tsize < 0)
                {
                    WvString message = WvString(
                        "Request for tsize of %s is invalid.  Aborting.",
                        newconn->tsize);
                    log(WvLog::Debug, "%s\n", message);
                    send_err(8, message);
                    delete newconn;
                    return;
                }
                else
                {
                    if (newconn->tsize == 0 && newconn->direction == tftpread)
                    {
                        struct stat * tftpfilestat = new struct stat;
                        if (stat(newconn->filename, tftpfilestat) != 0)
                        {
                            WvString message = 
                                "Cannot get stats for file.  Aborting.";
                           log(WvLog::Debug, "%s\n", message);
                           send_err(8, message);
                           delete newconn;
                           return;
                        }
                        newconn->tsize = tftpfilestat->st_size;
                        delete tftpfilestat;
                    }
                    WvString oacktsize = WvString("%s", newconn->tsize);
                    log("tsize option enabled (%s octets).\n",
                        newconn->tsize);
                    strcpy(oackp, optname);
                    oackp += strlen(optname) + 1;
                    newconn->oacklen += strlen(optname) + 1;
                    strcpy(oackp, oacktsize.edit());
                    oackp += oacktsize.len() + 1;
                    newconn->oacklen += oacktsize.len() + 1;
                }
            }
        }
        if (newconn->oacklen)
            newconn->send_oack = true;
        else
            delete newconn->oack;
        if (!strcmp(&packet[modestart], "netascii"))
            newconn->mode = netascii;
        else if (!strcmp(&packet[modestart], "octet"))
           newconn->mode = octet;
        else if (!strcmp(&packet[modestart], "mail"))
           newconn->mode = mail;
        else
        { 
            log(WvLog::Debug, "Unknown mode string \"%s\"; aborting.\n",
                &packet[modestart]);
            send_err(4);
            delete newconn;
            return;
        }
    }

    newconn->stamp = time(0);
    alarm(tftp_tick);
    conns.add(newconn, true);
    if (newconn->direction == tftpread)
    {
        newconn->lastsent = 0;
        newconn->unack = 1;
        if (newconn->send_oack)
        {
            log(WvLog::Debug5, "Sending oack ");
            memcpy(packet, newconn->oack, 512);
            packetsize = newconn->oacklen;
            dump_pkt();
            write(packet, packetsize);
        }
        else
        {
            log(WvLog::Debug5, "last sent: %s unack: %s pktclump: %s\n", newconn->lastsent,
                newconn->unack, newconn->pktclump);
            int pktsremain = static_cast<int>(newconn->lastsent) -
                static_cast<int>(newconn->unack);
            while (pktsremain < static_cast<int>(newconn->pktclump) - 1)
            {
                log(WvLog::Debug5, "result is %s\n", pktsremain);
                send_data(newconn);
                if (newconn->donefile)
                    break;
                pktsremain = static_cast<int>(newconn->lastsent) -
                    static_cast<int>(newconn->unack);
            }
        }
    }
    else
    {
        newconn->lastsent = 65535;
        send_ack(newconn);
    }
}

