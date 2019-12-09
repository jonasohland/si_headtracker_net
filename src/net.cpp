#include "net.h"
#include "debug.h"

// #define ZEROCONF_IP              0xA9FE2D01
#define ZEROCONF_IP              0x012DFEA9
#define ZEROCONF_SUBNET          0x0000FFFF
#define HEADTRACKER_SERVICE_PORT 11023

byte mac[] = { 0x00, 0x19, 0x7C, 0xEF, 0xFE, 0xED };

void si_eth_hwprepare()
{
    digitalWrite(SI_NET_RESET_PIN, HIGH);
}

void si_eth_hwreset()
{
    pinMode(SI_NET_RESET_PIN, OUTPUT);
    digitalWrite(SI_NET_RESET_PIN, LOW);
    // rebooting
    delay(500);
}

void si_eth_connect(si_conf_t* conf)
{
    if (conf->network_flags & _BV(1)) {    // DHCP enabled

        if (!Ethernet.begin(mac)) {
            Ethernet.begin(mac, IPAddress(ZEROCONF_IP));
            Ethernet.setSubnetMask(ZEROCONF_SUBNET);
        }
    }
    else {    // DHCP disabled

        Ethernet.begin(mac, IPAddress(conf->static_ipaddr));
        Ethernet.setSubnetMask(conf->static_subnet);
    }
}

void si_eth_init(EthernetUDP* socket, si_conf_t* running_conf)
{
    EEPROM.get(0, *running_conf);

    bool eep_update = false;

    // clear reboot flag if set
    if (running_conf->device_flags & _BV(1)) {
        running_conf->device_flags &= ~_BV(1);
        eep_update = true;
    }

    // clear stream enabled flag
    if (running_conf->device_flags & _BV(2)){
        running_conf->device_flags &= ~_BV(2);
        eep_update = true;
    }

    if (eep_update)
        EEPROM.put(0, *running_conf);

    si_eth_connect(running_conf);

    socket->begin(HEADTRACKER_SERVICE_PORT);

    EthernetBonjour.begin("si_htrk_01");

    EthernetBonjour.addServiceRecord(
        "tracker01._htr", HEADTRACKER_SERVICE_PORT, MDNSServiceUDP);
}

void si_eth_set(si_conf_t* nconf, si_conf_t* conf)
{
    *conf = *nconf;
    EEPROM.put(0, *conf);
}

uint8_t si_eth_pck_parse(EthernetUDP* socket, si_conf_t* conf, uint8_t* buffer)
{
    // buffer should be a complete packet
    si_conf_packet_t* p = (si_conf_packet_t*) buffer;

    // wrong preamble
    if (p->preamble != SI_PACKET_PREAMBLE) return 0;

    Serial.println(IPAddress(p->conf.static_ipaddr));
    Serial.println(IPAddress(p->conf.static_subnet));

    *conf = p->conf;

    // write current configuration to eeprom
    if (p->conf.device_flags & _BV(0)) si_eth_set(&p->conf, conf);

    return 1;
}

void si_eth_send_pck(EthernetUDP* socket, si_device_state_t* st, si_data_packet_t* p)
{
    socket->beginPacket(st->sender_ip, st->sender_port);
    socket->write((uint8_t*) p, sizeof(si_data_packet_t));
    socket->endPacket();
}

void si_eth_pck_respond(EthernetUDP* socket, si_conf_t* conf)
{
    socket->beginPacket(socket->remoteIP(), socket->remotePort());

    socket->write((uint8_t)(SI_PACKET_PREAMBLE << 24));
    socket->write((uint8_t)(SI_PACKET_PREAMBLE << 16));
    socket->write((uint8_t)(SI_PACKET_PREAMBLE << 8));
    socket->write((uint8_t) SI_PACKET_PREAMBLE);

    socket->write((uint8_t*) &conf, sizeof(si_conf_t));

    socket->endPacket();
}

void si_eth_recv(EthernetUDP* socket,
                 si_device_state_t* st,
                 si_conf_t* conf,
                 uint8_t* buffer)
{
    if (int psize = socket->parsePacket()) {

        if (psize > 256) return socket->flush();

        socket->readBytes(buffer, psize);

        socket->flush();

        if (si_eth_pck_parse(socket, conf, buffer)) {

            si_eth_pck_respond(socket, conf);

            st->sender_ip   = socket->remoteIP();
            st->sender_port = socket->remotePort();

            if (conf->device_flags & _BV(1)) si_eth_hwreset();
        }
    }
}

void si_eth_run(EthernetUDP* socket,
                si_device_state_t* st,
                si_conf_t* conf,
                uint8_t* buf)
{
    si_eth_recv(socket, st, conf, buf);

    EthernetBonjour.run();
    Ethernet.maintain();
}