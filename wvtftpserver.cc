/*
 * Worldvisions Weaver Software:
 *   Copyright (C) 1997-2001 Net Integration Technologies, Inc.
 */
#include <ctype.h>
#include "wvtftpserver.h"
#include "strutils.h"

WvTFTPServer::WvTFTPServer(WvString _basedir, int _tftp_tick, int def_timeout)
    : WvTFTPBase(_tftp_tick, def_timeout, 69), basedir(_basedir)
{
}

WvTFTPServer::~WvTFTPServer()
{
}

void WvTFTPServer::execute()
{
    int connscount = 0;
    WvTFTPServer::TFTPConnDict::Iter i(conns);
    for (i.rewind(); i.next(); )
    {
        connscount++;
        if (difftime(time(0), i().stamp) >= i().timeout)
        {
            log("Timeout on connection from %s.\n", i().client);
            if (i().direction == tftpread)
                send_data(&i(), true);
            else
                send_ack(&i(), true);
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
            handle_packet();
    }
}

void WvTFTPServer::new_connection()
{
    log(WvLog::Debug4, WvString("New connection from %s\n", remaddr));
    TFTPOpcode pktcode = static_cast<TFTPOpcode>(packet[0] * 256 + packet[1]);
    log(WvLog::Debug4, WvString("Packet opcode is %s.\n", pktcode));
    if (pktcode > WRQ)
    {
        log(WvLog::Debug4, "Erroneous packet; discarding.\n");
        return;
    }
    
    TFTPConn *newconn = new TFTPConn;
    newconn->client = remaddr;

    // Make sure the filename and mode actually end with nulls.
    bool foundnull1 = false;
    bool foundnull2 = false;
    unsigned int modestart;
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
        log(WvLog::Debug4, "Badly formed packet; discarding.\n");
        send_err(4);
        delete newconn;
        return;
    }

    newconn->filename = &packet[2];
    if (newconn->filename[0] != '/')
        newconn->filename = WvString("%s/%s", basedir, newconn->filename);
    log(WvLog::Debug4, WvString("Filename is %s.\n", newconn->filename));
    newconn->direction = static_cast<TFTPDir>(static_cast<int>(pktcode)-1);
    log(WvLog::Debug4, WvString("Direction is %s.\n", newconn->direction));

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
        log(WvLog::Debug4,
            WvString("Unknown mode string \"%s\"; discarding.\n",
            &packet[modestart]));
        send_err(4);
        delete newconn;
        return;
    }
    log(WvLog::Debug4, WvString("Mode is %s.\n", newconn->mode));

    newconn->timeout = def_timeout;
    newconn->blksize = 512;
    newconn->tsize = 0;
    newconn->pktclump = 3;
    newconn->unack = 0;
    newconn->donefile = false;

    newconn->send_oack = false;
    char oack[512];
    size_t oacklen = 0;
    // Look for options.
    ch++;
    if (ch < packetsize)
    {
        // One or more options available for reading.
        oack[0] = 0;
        oack[1] = 6;
        oacklen = 2;
        char * oackp = &oack[2];
        char * optname = &packet[ch];
        char * optvalue;
        while (ch < packetsize-1)
        {
            for (int i = 0; i < 2; i++)
            {
                while (packet[++ch])
                {
                    if (ch == packetsize)
                    {    
                        log(WvLog::Debug4,
                            WvString("Badly formed option %s.  Aborting.\n",
                            (i==0) ? "name" : "value"));
                        send_err(8);
                        delete newconn;
                        return;
                    }
                }
                if (!i)
                    optvalue = &packet[ch+1];
            }
            wvcon->print("Option %s, value %s.\n", optname, optvalue);
            strlwr(optname);
            wvcon->print("Option is now %s.\n", optname);

            if (!strcmp(optname, "blksize"))
            {
                newconn->blksize = atoi(optvalue);
                if (newconn->blksize < 8 || newconn->blksize > 65464)
                {
                    WvString message = WvString(
                        "Request for blksize of %s is invalid.  Aborting.",
                        newconn->blksize);
                    log(WvLog::Debug4, WvString("%s\n", message));
                    send_err(8, message);
                    delete newconn;
                    return;
                }
                else
                {
                    log(WvString("blksize option enabled (%s octets).\n",
                        newconn->blksize));
                    strcpy(oackp, optname);
                    oackp += strlen(optname) + 1;
                    oacklen += strlen(optname) + 1;
                    strcpy(oackp, optvalue);
                    oackp += strlen(optvalue) + 1;
                    oacklen += strlen(optvalue) + 1;
                }
            }
        }
        if (oacklen)
            newconn->send_oack = true;
        if (!strcmp(&packet[modestart], "netascii"))
            newconn->mode = netascii;
        else if (!strcmp(&packet[modestart], "octet"))
           newconn->mode = octet;
        else if (!strcmp(&packet[modestart], "mail"))
           newconn->mode = mail;
        else
        { 
            log(WvLog::Debug4,
                WvString("Unknown mode string \"%s\"; discarding.\n",
                &packet[modestart]));
            send_err(4);
            delete newconn;
            return;
        }
    }

    newconn->stamp = time(0);
    if (newconn->direction == tftpread)
    {
        if (newconn->mode == netascii)
            newconn->tftpfile = fopen(newconn->filename, "r");
        else if (newconn->mode == octet)
            newconn->tftpfile = fopen(newconn->filename, "rb");

        if (!newconn->tftpfile)
        {
            log(WvLog::Debug4, "Failed to open file for reading; aborting.\n");
            send_err(1);
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
            log(WvLog::Debug4, "File already exists; aborting.\n");
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
            log(WvLog::Debug4, "Failed to open file for writing; aborting.\n");
            send_err(3);
            delete newconn;
            return;
        }
    }
    alarm(tftp_tick);
    conns.add(newconn, true);
    if (newconn->direction == tftpread)
    {
        newconn->lastsent = 0;
        newconn->unack = 1;
        if (newconn->send_oack)
        {
            log(WvLog::Debug5, "Sending oack ");
            memcpy(packet, oack, 512);
            packetsize = oacklen;
            dump_pkt();
            write(packet, packetsize);
        }
        else
        {
            log("last sent: %s unack: %s pktclump: %s\n", newconn->lastsent,
                newconn->unack, newconn->pktclump);
            int pktsremain = static_cast<int>(newconn->lastsent) -
                static_cast<int>(newconn->unack);
            while (pktsremain < static_cast<int>(newconn->pktclump) - 1)
            {
                log("result is %s\n", pktsremain);
                log("send\n");
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

