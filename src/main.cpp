// clang-format off
#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>
#include <EEPROM.h>
// clang-format on

#define SI_UDP_TRACKING_PPS       25
#define SI_UDP_PACKET_TAG         0x93ADC7F5
#define SI_UDP_PACKET_TAG_SIZE    sizeof(uint32_t)
#define SI_UDP_PACKET_HEADER_SIZE sizeof(uint32_t) + sizeof(uint8_t)
#define SI_UDP_ALIVE_RESP_PORT    10551
#define SI_UDP_CONTROL_INPUT_PORT 10331
#define SI_FLAG_DHCP_DISABLED     0x45

byte buf[256];
byte sbuf[64];
byte mac[] = { 0x00, 0x19, 0x7C, 0xEF, 0xFE, 0xED };

unsigned long last_send = 0;
bool do_send            = false;

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

struct si_packet_header {
    uint32_t ident;
    packet_type ty;
};

struct si_ipinfo_packet {
    uint32_t ident;
    packet_type ty;
    uint8_t ip[4];
    uint8_t subnet[4];
};

int s = sizeof(IPAddress);

si_packet_header* get_header(byte* buf)
{
    return (si_packet_header*) buf;
}

void buffer_init(byte* buf)
{
    get_header(buf)->ident = SI_UDP_PACKET_TAG;
}

byte* pack_data_start(byte* buf)
{
    return buf + SI_UDP_PACKET_HEADER_SIZE;
}

bool pack_valid(byte* buf, size_t s)
{
    return (s >= 5) && (get_header(buf)->ident == SI_UDP_PACKET_TAG);
}

void pack_set_ty(byte* buf, packet_type t)
{
    get_header(buf)->ty = t;
}

bool pack_get_startup_addr(byte* buf,
                           size_t s,
                           IPAddress* addr,
                           IPAddress* subnet)
{

    uint8_t* addrdata = pack_data_start(buf);

    if (s <= 12) return true;

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
    unsigned long time = millis();

    if (time - last_send < 1000 / SI_UDP_TRACKING_PPS) return;

    last_send = time;

    if (!do_send) return;

    buffer_init(sbuf);
    pack_set_ty(sbuf, packet_type::track_stream_data);

    udps.beginPacket(stream_output_addr, stream_output_port);

    udps.write(sbuf, SI_UDP_PACKET_HEADER_SIZE);

    udps.endPacket();

    udps.flush();
}

void stream_disable()
{
    Serial.println("Tracker data streaming disabled");

    do_send = false;
}

void stream_enable()
{
    Serial.println("Tracker data streaming enabled");

    stream_output_port = udps.remotePort();
    stream_output_addr = udps.remoteIP();

    do_send = true;
}

void update_bonjour()
{
    Serial.println("Running Bonjour");
    EthernetBonjour.run();
}

void setup()
{
    Serial.begin(9600);

    delay(5000);


    Serial.println("Setup begin");

    // Ethernet.setCsPin(10);


    if (!should_use_dhcp()) {

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

    // Ethernet.phyMode(phyMode_t::FULL_DUPLEX_100);

    int status = EthernetBonjour.begin("siheadtracker");

    if (status != 1) {
        Serial.print("Could not initialize Bonjour Library. Code: ");
        Serial.println(status);
    }

    EthernetBonjour.addServiceRecord("headtracker._trackingstream",
                                     SI_UDP_CONTROL_INPUT_PORT,
                                     MDNSServiceUDP);

    Serial.println("Setup done");

    udps.begin(SI_UDP_CONTROL_INPUT_PORT);
}

void loop()
{
    if (int psize = udps.parsePacket()) {

        if (psize > 256) return;

        udps.readBytes(buf, psize);

        if (pack_valid(buf, psize)) {

            switch (get_header(buf)->ty) {
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
    tracker_run();
    EthernetBonjour.run();
}