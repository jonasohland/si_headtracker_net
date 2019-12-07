// clang-format off
#define DEBUG
#define I2CDEV_IMPLEMENTATION I2CDEV_BUILTIN_FASTWIRE

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>
#include <EEPROM.h>
#include <MPU6050_6Axis_MotionApps20.h>
// clang-format on

#include "net.h"

#define SI_UDP_TRACKING_PPS       50
#define SI_UDP_PACKET_TAG         0x93ADC7F5
#define SI_UDP_PACKET_TAG_SIZE    sizeof(uint32_t)
#define SI_UDP_PACKET_HEADER_SIZE sizeof(uint32_t) + sizeof(uint8_t)
#define SI_UDP_ALIVE_RESP_PORT    10551
#define SI_UDP_CONTROL_INPUT_PORT 10331
#define SI_FLAG_DHCP_DISABLED     0x45

byte buf[256];
byte sbuf[64];
byte mpubuf[64];
byte mac[] = { 0x00, 0x19, 0x7C, 0xEF, 0xFE, 0xED };

Quaternion qbuf[5];
Quaternion* qbuf_ptr = qbuf;

int mpu_expected_packet_size = 0;

int16_t blink = 0;

unsigned long last_send = 0;
bool do_send            = false;

EthernetUDP udps;
MPU6050 mpu;

IPAddress zerconf_addr(192,168,0,135);
IPAddress zerconf_subnet(255,255,255,0);

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
    socket->beginPacket(socket->remoteIP(), socket->remotePort());
    pack_set_ty(buf, packet_type::alive_resp);
    socket->write(buf, s);
    return socket->endPacket() == 1;
}

void set_dhcp_enabled(byte* buf, size_t s)
{
    uint8_t* data = pack_data_start(buf);

    EEPROM.write(0, *data);
}

void set_startup_addr(byte* buf, size_t s)
{
    IPAddress ip;
    IPAddress subnet;

    if (pack_get_startup_addr(buf, s, &ip, &subnet)) return;

    uint8_t* ipdata = pack_data_start(buf);

    for (int i = 0; i < 8; ++i) EEPROM.write(i + 1, ipdata[i]);

    EEPROM.write(0, SI_FLAG_DHCP_DISABLED);
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

    /*blink = !blink;
    digitalWrite(3, blink);
*/

    if(!mpu.testConnection()){
                
        digitalWrite(3, 1);
        
        Fastwire::setup(100, true);

        Serial.println("init");
        mpu.initialize();

        Serial.println("dmp_init");
        mpu.dmpInitialize();

        Serial.println("dmp_enable");
        mpu.setDMPEnabled(true);

        digitalWrite(3, 0);
    }

    if (!do_send) return;

    int mpuIntStatus = mpu.getIntStatus();
    int fifocnt      = mpu.getFIFOCount();

    Quaternion q;

    if (mpuIntStatus & _BV(MPU6050_INTERRUPT_DMP_INT_BIT)
        && fifocnt >= mpu_expected_packet_size) {

        mpu.getFIFOBytes(mpubuf, mpu_expected_packet_size);
        mpu.dmpGetQuaternion(&q, mpubuf);
        mpu.resetFIFO();
    }
    else return;

    buffer_init(sbuf);
    pack_set_ty(sbuf, packet_type::track_stream_data);

    udps.beginPacket(stream_output_addr, stream_output_port);

    memcpy(pack_data_start(sbuf), &q, sizeof(Quaternion));

    udps.write(sbuf, SI_UDP_PACKET_HEADER_SIZE + sizeof(Quaternion));

    udps.endPacket();

    udps.flush();

    digitalWrite(3, 1);
}

void stream_disable()
{
    do_send = false;
}

void stream_enable()
{
    stream_output_port = udps.remotePort();
    stream_output_addr = udps.remoteIP();

    do_send = true;
}

void update_bonjour()
{
    Serial.println("Running Bonjour");
    EthernetBonjour.run();
}

void eth_init_static()
{
        IPAddress ip, subnet;
        get_startup_addr(&ip, &subnet);

        Serial.print("Using fixed IP Address: ");
        Serial.print(ip);
        Serial.print(" ");
        Serial.println(subnet);

        Ethernet.begin(mac, ip, subnet);
}

void setup()
{
    Serial.begin(9600);

    Fastwire::setup(100, true);
    I2Cdev::readTimeout = 5;

    pinMode(3, OUTPUT);
    pinMode(4, OUTPUT);

    digitalWrite(3, 1);
    digitalWrite(4, 1);

    delay(2000);

    digitalWrite(3, 0);
    digitalWrite(4, 0);

    mpu.initialize();

    bool DHCP_SUCCES = false;

    if (!should_use_dhcp()){
        DHCP_SUCCES = true; 
        eth_init_static();
    }
    else {
        if(!Ethernet.begin(mac))
            Ethernet.begin(mac, zerconf_addr, zerconf_subnet);

        DHCP_SUCCES = true;
    }

    int status = EthernetBonjour.begin("si_htr_001");

    if (status != 1) {
        Serial.print("Could not initialize Bonjour Library. Code: ");
        Serial.println(status);
    }

    Ethernet.maintain();

    EthernetBonjour.addServiceRecord(
        "tracker._trs", SI_UDP_CONTROL_INPUT_PORT, MDNSServiceUDP);

    udps.begin(SI_UDP_CONTROL_INPUT_PORT);


    bool devStatus = mpu.dmpInitialize();

    // supply your own gyro offsets here, scaled for min sensitivity
    mpu.setXGyroOffset(220);
    mpu.setYGyroOffset(76);
    mpu.setZGyroOffset(-85);
    mpu.setZAccelOffset(1788);    // 1688 factory default for my test chip

    mpu.setDMPEnabled(true);

    mpu_expected_packet_size = mpu.dmpGetFIFOPacketSize();

    Serial.println("Setup done");

    delay(500);

    digitalWrite(3, devStatus == 0);
    digitalWrite(4, DHCP_SUCCES);
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
    Ethernet.maintain();
    EthernetBonjour.run();
}