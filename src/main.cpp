// clang-format off
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet3.h>
#include <EthernetBonjour.h>
#include <TimerOne.h>
#include <EEPROM.h>
// clang-format on

#define SI_UDP_PACKET_TAG         0x93ADC7F5
#define SI_UDP_PACKET_TAG_SIZE    sizeof(uint32_t)
#define SI_UDP_PACKET_HEADER_SIZE sizeof(uint32_t) + sizeof(uint8_t)
#define SI_UDP_ALIVE_RESP_PORT    10551
#define SI_UDP_CONTROL_INPUT_PORT 10331
#define SI_FLAG_DHCP_DISABLED     0x45

byte buf[256];
byte sbuf[64];
byte mac[] = { 0x00, 0x19, 0x7C, 0xEF, 0xFE, 0xED };
EthernetUDP udps;

IPAddress stream_output_addr;
uint16_t stream_output_port;

enum class packet_type : byte {
    error = 0,
    reset,
    startup_addr,
    set_dhcp_enabled,
    startup_addr_confirm,
    alive_req,
    alive_resp,
    track_stream_data,
    track_stream_enable,
    track_stream_enable_resp,
    track_stream_disable,
    track_stream_disable_resp,
};

void buffer_init(byte* buf)
{
    *((uint32_t*) buf) = SI_UDP_PACKET_TAG;
}

byte* pack_data_start(byte* buf)
{
    return buf + SI_UDP_PACKET_HEADER_SIZE;
}

bool pack_valid(byte* buf, size_t s)
{
    return (s >= 5) && (SI_UDP_PACKET_TAG == *((uint32_t*) buf));
}

void pack_set_ty(byte* buf, packet_type t)
{
    *(buf + SI_UDP_PACKET_TAG_SIZE) = (byte) t;
}

packet_type pack_get_ty(byte* buf, size_t size)
{
    if (not size) return packet_type::error;

    return (packet_type) * (buf + SI_UDP_PACKET_TAG_SIZE);
}

bool pack_get_startup_addr(byte* buf,
                           size_t s,
                           IPAddress* addr,
                           IPAddress* subnet)
{

    uint8_t* addrdata = pack_data_start(buf);

    if (s <= 13) return true;

    *addr   = IPAddress(addrdata[0], addrdata[1], addrdata[2], addrdata[3]);
    *subnet = IPAddress(addrdata[4], addrdata[5], addrdata[6], addrdata[7]);

    return false;
}

bool alive_req(EthernetUDP* socket, byte* buf, size_t s)
{
    Serial.println("Responding to alive request");
    socket->beginPacket(socket->remoteIP(), socket->remotePort());
    pack_set_ty(buf, packet_type::alive_resp);
    socket->write(buf, s);
    return socket->endPacket() == 1;
}

void set_dhcp_enabled(byte* buf, size_t s)
{
    uint8_t* data = pack_data_start(buf);

    if (*data == SI_FLAG_DHCP_DISABLED)
        Serial.println("DHCP disabled");
    else
        Serial.println("DHCP enabled");

    EEPROM.write(0, *data);
}

void set_startup_addr(byte* buf, size_t s)
{
    Serial.println("Setting startup address: ");

    IPAddress ip;
    IPAddress subnet;
    
    if (pack_get_startup_addr(buf, s, &ip, &subnet)) return;

    uint8_t* ipdata = pack_data_start(buf);

    for (int i = 0; i < 8; ++i) EEPROM.write(i + 1, ipdata[i]);

    EEPROM.write(0, SI_FLAG_DHCP_DISABLED);

    Serial.println("Written new IP Address and Subnet Mask to EEPROM");
}

bool should_use_dhcp()
{
    return EEPROM.read(0) != SI_FLAG_DHCP_DISABLED;
}

void get_startup_addr(IPAddress* ip, IPAddress* subnet)
{
    uint8_t ipbuf[8];

    for (int i = 0; i < 8; ++i) ipbuf[i] = EEPROM.read(i + 1);

    *ip     = IPAddress(ipbuf[0], ipbuf[1], ipbuf[2], ipbuf[3]);
    *subnet = IPAddress(ipbuf[4], ipbuf[5], ipbuf[6], ipbuf[7]);
}

void tracker_run()
{
    buffer_init(sbuf);
    pack_set_ty(sbuf, packet_type::track_stream_data);

    udps.beginPacket(stream_output_addr, stream_output_port);

    udps.write(sbuf, SI_UDP_PACKET_HEADER_SIZE);

    udps.endPacket();
}

void stream_disable()
{
    Serial.println("Tracker data streaming disabled");
    Timer1.stop();
}

void stream_enable()
{
    Serial.println("Tracker data streaming enabled");

    stream_output_port = udps.remotePort();
    stream_output_addr = udps.remoteIP();

    Timer1.initialize(5000);
    Timer1.attachInterrupt(tracker_run);
}

void setup()
{
    Serial.begin(9600);

    Serial.println("Setup begin");

    Ethernet.setCsPin(10);


    if (should_use_dhcp()) {

        IPAddress ip, subnet;
        get_startup_addr(&ip, &subnet);

        Serial.print("Using fixed IP Address: ");
        Serial.print(ip);
        Serial.print(" ");
        Serial.println(subnet);

        Ethernet.begin(mac, ip, subnet);
    }
    else
        Ethernet.begin(mac);

    int status = EthernetBonjour.begin("si_headtracker");

    if (status != 1) {
        Serial.print("Could not initialize Bonjour Library. Code: ");
        Serial.println(status);
    }

    EthernetBonjour.addServiceRecord(
        "headtracker._tracking_stream", 10452, MDNSServiceUDP);

    Serial.println("Setup done");

    udps.begin(3456);
}

void loop()
{
    noInterrupts();
    if (int psize = udps.parsePacket()) {

        if (psize > 256) return;

        Serial.print("Received packet of size ");
        Serial.println(psize);
        Serial.print("From ");

        IPAddress remote = udps.remoteIP();

        for (int i = 0; i < 4; i++) {
            Serial.print(remote[i], DEC);
            if (i < 3) { Serial.print("."); }
        }

        Serial.print(", port ");
        Serial.println(udps.remotePort());

        udps.readBytes(buf, psize);

        if (pack_valid(buf, psize)) {

            Serial.print("Packet Type: ");
            Serial.println((int) pack_get_ty(buf, psize));

            switch (pack_get_ty(buf, psize)) {
                case packet_type::error:
                    Serial.println("Received Error Packet");
                    break;
                case packet_type::reset:
                    Serial.println("Performing reset");
                    break;
                case packet_type::startup_addr:
                    set_startup_addr(buf, psize);
                    break;
                case packet_type::set_dhcp_enabled:
                    set_dhcp_enabled(buf, psize);
                    break;
                case packet_type::alive_req:
                    alive_req(&udps, buf, psize);
                    break;
                case packet_type::track_stream_enable: stream_enable(); break;
                case packet_type::track_stream_disable: stream_disable(); break;
                default: break;
            }
        }
        else {
            Serial.println("Invalid packet");
        }
    }
    EthernetBonjour.run();
    interrupts();
}