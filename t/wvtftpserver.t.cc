#include "uniconfroot.h"
#include "wvconf.h"
#include "wvfile.h"
#include "wvfileutils.h"
#include "wvistreamlist.h"
#include "wvstrutils.h"
#include "wvtest.h"
#define private public
#include "../wvtftpserver.h"
#undef private

#define PACKETS_EQ(ref_packet,rcvd_packet)                              \
    WVPASS((ref_packet));                                               \
    if ((ref_packet))                                                   \
    {                                                                   \
        WVPASSEQ((rcvd_packet).length, (ref_packet)->length);           \
        WVPASSEQ(memcmp((rcvd_packet).packet, (ref_packet)->packet,     \
                        (rcvd_packet).length),                          \
                 0);                                                    \
    }                                                                   \


class WvTftpServerTester
{
public:
    WvTftpServerTester(WvStringParm uniconf_moniker = "temp:")
        : cfg(uniconf_moniker),
          base_dir("/tmp/wvtftpd-%s.%s", time(NULL), getpid())
    {
        cfg["TFTP/Port"].setmeint(6969);
        cfg["TFTP/Base dir"].setme(base_dir);
        mkdir(base_dir, 0777);
        tftp_server = new WvTFTPServer(cfg, 100);
    }

    ~WvTftpServerTester()
    {
        WVRELEASE(tftp_server);
        rm_rf(base_dir);
    }

    UniConfRoot cfg;
    WvString base_dir;
    WvTFTPServer *tftp_server;

    void create_file(WvStringParm name, off_t size)
    {
        WvFile f(WvString("%s/%s", base_dir, name),
                 O_WRONLY | O_CREAT | O_TRUNC);
        off_t current_size = 0;
        while (current_size < size)
        {
            unsigned int buf_size = (size - current_size) < 256 ?
                                    (size - current_size) : 256;
            unsigned char buf[buf_size];
            current_size += buf_size;
            for (unsigned int i = 0; i < buf_size; i++)
                buf[i] = i;
            f.write(buf, buf_size);
        }
    }

private:
};


struct TftpPacket
{
    unsigned char *packet;
    unsigned int length;

    TftpPacket() : packet(NULL), length(0) { }
    ~TftpPacket() { deletev packet; }
};


TftpPacket *rq_packet(WvTFTPBase::TFTPDir direction, WvString filename,
                         WvTFTPBase::TFTPMode mode)
{
    WvString mode_string;
    switch (mode)
    {
    case WvTFTPBase::netascii:
        mode_string = "netascii";
        break;
    case WvTFTPBase::octet:
        mode_string = "octet";
        break;
    case WvTFTPBase::mail:
        mode_string = "mail";
        break;
    default:
        return NULL;
    }

    unsigned int packet_length = 4 + filename.len() + mode_string.len();
    unsigned char *packet = new unsigned char[packet_length];
    packet[0] = 0;
    packet[1] = (unsigned char)(direction == WvTFTPBase::tftpread
                                ? WvTFTPBase::RRQ : WvTFTPBase::WRQ);
    memcpy(&packet[2], filename.cstr(), filename.len());
    packet[2 + filename.len()] = 0;
    memcpy(&packet[3 + filename.len()], mode_string.cstr(), mode_string.len());
    packet[3 + filename.len() + mode_string.len()] = 0;

    TftpPacket *tftp_packet = new TftpPacket;
    tftp_packet->packet = packet;
    tftp_packet->length = packet_length;

    return tftp_packet;
}


TftpPacket *data_packet(unsigned int block_num, unsigned char *data,
                        unsigned int data_length)
{
    if (block_num > 65535)
        return NULL;
    if (data_length > 512)
        return NULL;

    unsigned int packet_length = 4 + data_length;
    unsigned char *packet = new unsigned char[packet_length];
    packet[0] = 0;
    packet[1] = (unsigned char)WvTFTPBase::DATA;
    packet[2] = (block_num % 65536) / 256;
    packet[3] = (block_num % 65536) % 256;
    if (data && data_length)
        memcpy(&packet[4], data, data_length);

    TftpPacket *tftp_packet = new TftpPacket;
    tftp_packet->packet = packet;
    tftp_packet->length = packet_length;

    return tftp_packet;
}


TftpPacket *ack_packet(unsigned int block_num)
{
    if (block_num > 65535)
        return NULL;

    unsigned int packet_length = 4;
    unsigned char *packet = new unsigned char[packet_length];
    packet[0] = 0;
    packet[1] = (unsigned char)WvTFTPBase::ACK;
    packet[2] = (block_num % 65536) / 256;
    packet[3] = (block_num % 65536) % 256;

    TftpPacket *tftp_packet = new TftpPacket;
    tftp_packet->packet = packet;
    tftp_packet->length = packet_length;

    return tftp_packet;
}


TftpPacket *error_packet(unsigned int error_code, WvStringParm err_message)
{
    if (error_code > 65535)
        return NULL;

    unsigned int packet_length = 5 + err_message.len();
    unsigned char *packet = new unsigned char[packet_length];
    packet[0] = 0;
    packet[1] = (unsigned char)WvTFTPBase::ERROR;
    packet[2] = (error_code % 65536) / 256;
    packet[3] = (error_code % 65536) % 256;
    memcpy(&packet[4], err_message.cstr(), err_message.len());
    packet[4 + err_message.len()] = 0;

    TftpPacket *tftp_packet = new TftpPacket;
    tftp_packet->packet = packet;
    tftp_packet->length = packet_length;

    return tftp_packet;
}


void display_packet(TftpPacket *tftp_packet)
{
    if (!tftp_packet)
        wvcon->print("No packet!");
    else
        wvcon->print("Packet:\n%s\n", hexdump_buffer(tftp_packet->packet,
                                                     tftp_packet->length));
}


void get_response_packet(WvUDPStream &udp, WvTFTPServer &server,
                         TftpPacket &tftp_packet)
{
    tftp_packet.length = 0;
    deletev tftp_packet.packet;
    tftp_packet.packet = new unsigned char[516];

    while (!tftp_packet.length && server.isok() && udp.isok())
    {
        if (udp.isreadable())
            tftp_packet.length = udp.read(tftp_packet.packet, 516);
        else
            WvIStreamList::globallist.runonce();
    }
}


WVTEST_MAIN("check_filename")
{
    UniConfRoot cfg("temp:");
    WvTftpServerTester tester;
    WvFile f(WvString("%s/default", tester.base_dir),
             O_WRONLY | O_CREAT | O_TRUNC);
    f.close();
    f.open(WvString("%s/foo", tester.base_dir),
             O_WRONLY | O_CREAT | O_TRUNC);
    f.close();
    f.open(WvString("%s/real_file", tester.base_dir),
             O_WRONLY | O_CREAT | O_TRUNC);
    f.close();
    f.open(WvString("%s/once", tester.base_dir),
             O_WRONLY | O_CREAT | O_TRUNC);
    f.close();
    WvTFTPBase::TFTPConn conn;
    conn.remote = "192.168.1.1";
    conn.direction = WvTFTPBase::tftpread;
    conn.mode = WvTFTPBase::octet;
    
    // Full path.
    conn.filename = WvString("%s/foo", tester.base_dir);
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/foo", tester.base_dir));

    // Relative path.
    conn.filename = "foo";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/foo", tester.base_dir));

    // Strip prefix.
    tester.cfg["TFTP/Strip prefix"].setme("/strip");
    conn.filename = "/strip/foo";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/foo", tester.base_dir));

    // Missing file.
    conn.filename = "/foo";
    WVFAIL(tester.tftp_server->check_filename(&conn));

    // Parent directory.
    conn.filename = WvString("%s/../%s/secret", tester.base_dir,
                             tester.base_dir);
    WVFAIL(tester.tftp_server->check_filename(&conn));

    // Default file.
    tester.cfg["TFTP/Default file"].setme("default");
    conn.filename = "notfound";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/default", tester.base_dir));

    // Default relative alias.
    tester.cfg["TFTP/Aliases/default/aliased_file"].setme("real_file");
    conn.filename = "aliased_file";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/real_file", tester.base_dir));

    // Default full-path alias.
    tester.cfg["TFTP/Aliases/default/aliased_file"].remove();
    tester.cfg["TFTP/Aliases/default"][tester.base_dir]["aliased_file"]
        .setme(WvString("%s/real_file", tester.base_dir));
    conn.filename = WvString("%s/aliased_file", tester.base_dir);
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/real_file", tester.base_dir));

    // IP-specific relative alias.
    tester.cfg["TFTP/Aliases/192.168.1.1/aliased_file"].setme("real_file");
    conn.filename = "aliased_file";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/real_file", tester.base_dir));

    // IP-specific full-path alias.
    tester.cfg["TFTP/Aliases/192.168.1.1"][tester.base_dir]["aliased_file"]
        .setme(WvString("%s/real_file", tester.base_dir));
    conn.filename = WvString("%s/aliased_file", tester.base_dir);
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/real_file", tester.base_dir));

    // Alias once (just check priority, as alias once is only cleared at
    // the end of the transfer)..
    tester.cfg["TFTP/Alias Once/192.168.1.1/aliased_file"].setme("once");
    conn.filename = "aliased_file";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/once", tester.base_dir));

    conn.direction = WvTFTPBase::tftpwrite;
    tester.cfg["TFTP/Client Directory"].setmeint(1);
    conn.filename = "upload";
    WVFAIL(tester.tftp_server->check_filename(&conn));
    
    tester.cfg["TFTP/Create Client Directory"].setmeint(1);
    conn.filename = "upload";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/192.168.1.1/upload",
                                     tester.base_dir));
    struct stat st;
    WVPASSEQ(stat(WvString("%s/192.168.1.1", tester.base_dir), &st), 0);

    tester.cfg["TFTP/Create Client Directory"].setmeint(0);
    conn.filename = "upload";
    WVPASS(tester.tftp_server->check_filename(&conn));
    WVPASSEQ(conn.filename, WvString("%s/192.168.1.1/upload",
                                     tester.base_dir));
}


WVTEST_MAIN("WvConf conversion")
{
    WvString ini_name("/tmp/wvtftpd.ini.%s.%s", time(NULL), getpid());
    {
        WvConf cfg(ini_name);
        cfg.set("TFTP Aliases", "a", "b");
        cfg.set("TFTP Aliases", "/c/d", "/e/f");
        cfg.set("TFTP Aliases", "127.0.0.1 g", "h");
        cfg.set("TFTP Aliases", "192.168.1.1 /i/j", "/k/l");
        cfg.set("TFTP Alias Once", "m", "n");
        cfg.set("TFTP Alias Once", "/o/p", "/q/r");
        cfg.set("TFTP Alias Once", "127.0.0.1 s", "t");
        cfg.set("TFTP Alias Once", "192.168.1.1 /u/v", "/w/x");
        cfg.setint("TFTP", "Port", 6969);
        cfg.save();
    }

    WvTftpServerTester tester(WvString("ini:%s", ini_name));

    {
        UniConfRoot uni(WvString("ini:%s", ini_name));
        WVPASSEQ(uni["TFTP/Aliases/default/a"].getme(), "b");
        WVPASSEQ(uni["TFTP/Aliases/default/c/d"].getme(), "/e/f");
        WVPASSEQ(uni["TFTP/Aliases/127.0.0.1/g"].getme(), "h");
        WVPASSEQ(uni["TFTP/Aliases/192.168.1.1/i/j"].getme(), "/k/l");
        WVPASSEQ(uni["TFTP/Alias Once/default/m"].getme(), "n");
        WVPASSEQ(uni["TFTP/Alias Once/default/o/p"].getme(), "/q/r");
        WVPASSEQ(uni["TFTP/Alias Once/127.0.0.1/s"].getme(), "t");
        WVPASSEQ(uni["TFTP/Alias Once/192.168.1.1/u/v"].getme(), "/w/x");
    }

    unlink(ini_name);
}



WVTEST_MAIN("read protocol")
{
    WvTftpServerTester tester;
    WvIStreamList::globallist.append(tester.tftp_server, false, "TFTP server");

    wvcon->print("Connecting to server...\n");
    WvUDPStream udp("127.0.0.1", "127.0.0.1:6969");
    WVPASS(udp.isok());
    WvIStreamList::globallist.append(&udp, false, "TFTP client");

    while (!udp.iswritable())
        WvIStreamList::globallist.runonce();

    wvcon->print("Connected.\n");

    /** Try a file with a multiple of 512 as its size. **/

    tester.create_file("foo", 512);

    TftpPacket *packet = NULL;  // for sending and comparison
    TftpPacket rcvd_packet;     // for packet received from server

    // Send read request.
    packet = rq_packet(WvTFTPBase::tftpread, "foo", WvTFTPBase::octet);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    // Verify received data packet.
    unsigned char databuf[512];
    for (unsigned int i = 0; i < 512; i++)
        databuf[i] = i;

    WVDELETE(packet);
    packet = data_packet(1, databuf, 512);
    PACKETS_EQ(packet, rcvd_packet);

    // Send ack packet.
    WVDELETE(packet);
    packet = ack_packet(1);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    // Verify final data packet, which should be 0 length since the file is
    // exactly 512 bytes.
    WVDELETE(packet);
    packet = data_packet(2, NULL, 0);
    PACKETS_EQ(packet, rcvd_packet);

    WVDELETE(packet);

    /** Try a file of with size not a multiple of 512. **/
    tester.create_file("foo2", 768);

    // Send read request.
    packet = rq_packet(WvTFTPBase::tftpread, "foo2", WvTFTPBase::octet);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    // Verify first received data packet.
    WVPASS(rcvd_packet.length);

    WVDELETE(packet);
    packet = data_packet(1, databuf, 512);
    PACKETS_EQ(packet, rcvd_packet);

    // Send ack packet.
    WVDELETE(packet);
    packet = ack_packet(1);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    // Verify second received data packet.
    WVPASS(rcvd_packet.length);

    WVDELETE(packet);
    packet = data_packet(2, databuf, 256);
    PACKETS_EQ(packet, rcvd_packet);

    // Send ack packet.
    WVDELETE(packet);
    packet = ack_packet(2);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);

    WVDELETE(packet);

    WvIStreamList::globallist.unlink(tester.tftp_server);
    WvIStreamList::globallist.unlink(&udp);
}


WVTEST_MAIN("write protocol")
{
    WvTftpServerTester tester;
    WvIStreamList::globallist.append(tester.tftp_server, false, "TFTP server");
    tester.cfg["TFTP/Readonly"].setmeint(0);

    wvcon->print("Connecting to server...\n");
    WvUDPStream udp("127.0.0.1", "127.0.0.1:6969");
    WVPASS(udp.isok());
    WvIStreamList::globallist.append(&udp, false, "TFTP client");

    while (!udp.iswritable())
        WvIStreamList::globallist.runonce();

    wvcon->print("Connected.\n");

    TftpPacket *packet = NULL;  // for sending and comparison
    TftpPacket rcvd_packet;     // for packet received from server

    /** Try a file with a multiple of 512 as its size. **/

    // Send write request.
    packet = rq_packet(WvTFTPBase::tftpwrite, "foo", WvTFTPBase::octet);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    // Verify received data packet.
    WVDELETE(packet);
    packet = ack_packet(0);
    PACKETS_EQ(packet, rcvd_packet);
    wvcon->print("%s\n", hexdump_buffer(rcvd_packet.packet, rcvd_packet.length));

    unsigned char databuf[512];
    for (unsigned int i = 0; i < 512; i++)
        databuf[i] = i;

    WVDELETE(packet);
    packet = data_packet(1, databuf, 512);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    WVDELETE(packet);
    packet = ack_packet(1);
    PACKETS_EQ(packet, rcvd_packet);

    // Final data packet must be 0 length since the file is exactly 512 bytes.
    WVDELETE(packet);
    packet = data_packet(2, NULL, 0);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    WVDELETE(packet);
    packet = ack_packet(2);
    PACKETS_EQ(packet, rcvd_packet);

    WVDELETE(packet);

    /** Try a file of with size not a multiple of 512. **/

    // Send write request.
    packet = rq_packet(WvTFTPBase::tftpwrite, "foo2", WvTFTPBase::octet);
    WVPASS(packet);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    // Verify received data packet.
    WVDELETE(packet);
    packet = ack_packet(0);
    PACKETS_EQ(packet, rcvd_packet);
    wvcon->print("%s\n", hexdump_buffer(rcvd_packet.packet, rcvd_packet.length));

    WVDELETE(packet);
    packet = data_packet(1, databuf, 512);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    WVDELETE(packet);
    packet = ack_packet(1);
    PACKETS_EQ(packet, rcvd_packet);

    WVDELETE(packet);
    packet = data_packet(2, databuf, 256);

    udp.write(packet->packet, packet->length);
    get_response_packet(udp, *tester.tftp_server, rcvd_packet);

    WVDELETE(packet);
    packet = ack_packet(2);
    PACKETS_EQ(packet, rcvd_packet);

    WVDELETE(packet);

    WvIStreamList::globallist.unlink(tester.tftp_server);
    WvIStreamList::globallist.unlink(&udp);
}
