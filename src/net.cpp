#include "net.h"
#include "debug.h"

// #define ZEROCONF_IP              0xA9FE2D01
#define ZEROCONF_IP              0x012DFEA9
#define ZEROCONF_SUBNET          0x0000FFFF
#define HEADTRACKER_SERVICE_PORT 11023

byte mac[] = { 0x00, 0x19, 0x7C, 0xEF, 0xFE, 0xED };

void si_eth_hwprepare()
{
    digitalWrite(SI_HWRESET_PIN, HIGH);
    digitalWrite(SI_ETH_RESET_PIN, LOW);
}

void si_eth_hwreset()
{
    EthernetBonjour.removeServiceRecord(HEADTRACKER_SERVICE_PORT, MDNSServiceUDP);
    // WZ5500 might still be sending packets
    delay(1000);

    // pinMode(SI_HWRESET_PIN, OUTPUT);
    // digitalWrite(SI_HWRESET_PIN, LOW);
    SCB_AIRCR = 0x05FA0004;

    // rebooting
    delay(1000);
}

void si_eth_connect(si_conf_t* conf)
{
    if (SI_NET_FLAG(conf, SI_FLAG_NET_DHCP)) {    // DHCP enabled

        if (!Ethernet.begin(mac)) {

            // disable dhcp
            conf->network_flags &= ~SI_FLAG_NET_DHCP;
            si_eth_store(conf);

            // reboot
            si_eth_hwreset();
        }
    }
    else {    // DHCP disabled

        Ethernet.begin(mac, IPAddress(conf->local_ip));
        Ethernet.setSubnetMask(conf->local_subnet);
    }
}

void si_eth_init(EthernetUDP* socket,
                 si_conf_t* running_conf,
                 char* bnj_host,
                 char* bnj_serv)
{
    digitalWrite(SI_ETH_RESET_PIN, LOW);
    delay(500);


    bool eep_update = false;

    if(!digitalRead(SI_NET_RESET_CONF_PIN)) {

        running_conf->local_ip = IPAddress(169,254,145,23);
        running_conf->local_subnet = IPAddress(255,255,255,0);
        running_conf->network_flags &= ~SI_FLAG_NET_DHCP;
        running_conf->sampling_freq = 25;
        running_conf->status_flags &= ~(SI_FLAG_ST_INVERT_X | SI_FLAG_ST_INVERT_Y | SI_FLAG_ST_INVERT_Z);
        
        eep_update = true;

    } else 
        EEPROM.get(0, *running_conf);
    

    running_conf->device_flags
        &= ~(SI_FLAG_CFG_DEV_RESET | SI_FLAG_CFG_STR_ENABLED | SI_FLAG_CFG_NO_REQ);

    running_conf->status_flags
        &= ~(SI_FLAG_ST_GY_CONNECTED | SI_FLAG_ST_GY_READY);

    // clear reboot or stream flag if set
    if ((running_conf->device_flags & SI_FLAG_CFG_DEV_RESET)
        || (running_conf->device_flags & SI_FLAG_CFG_STR_ENABLED))
        eep_update = true;

    if (eep_update) EEPROM.put(0, *running_conf);

    si_eth_connect(running_conf);

    socket->begin(HEADTRACKER_SERVICE_PORT);

    char n0     = '0' + ((running_conf->network_flags >> 2) / 10);
    char n1     = '0' + ((running_conf->network_flags >> 2) % 10);
    bnj_host[8] = n0;
    bnj_host[9] = n1;
    bnj_serv[8] = n0;
    bnj_serv[9] = n1;

    EthernetBonjour.begin(bnj_host);

    EthernetBonjour.addServiceRecord(
        bnj_serv, HEADTRACKER_SERVICE_PORT, MDNSServiceUDP);
}

void si_eth_store(si_conf_t* nconf)
{
    EEPROM.put(0, *nconf);
}

uint8_t si_eth_pck_parse(EthernetUDP* socket, si_conf_t* conf, uint8_t* buffer)
{
    // buffer should be a complete packet
    si_conf_packet_t* p = (si_conf_packet_t*) buffer;

    // wrong preamble
    if (p->preamble != SI_PACKET_PREAMBLE) return 0;

    return 1;
}

void si_eth_send_pck(EthernetUDP* socket,
                     si_conf_t* conf,
                     si_device_state_t* st,
                     si_data_packet_t* p)
{
    p->device_id = (conf->network_flags >> 2);

    socket->beginPacket(conf->tgt_addr, conf->tgt_port);
    socket->write((uint8_t*) p, sizeof(si_data_packet_t));
    socket->endPacket();

    st->led_states ^= SI_NET_ST_LED;
    digitalWrite(SI_NET_STATUS_PIN, (st->led_states & SI_NET_ST_LED));
}

void si_eth_conf_update(si_device_state_t* st,
                        si_conf_t* conf,
                        si_conf_packet_t* pck)
{
    *conf = pck->conf;
    si_eth_conf_update(st, conf);
}

void si_eth_conf_update(si_device_state_t* st, si_conf_t* conf)
{
    if (st->mpu_status == SI_MPU_CONNECTED) {
        conf->status_flags |= SI_FLAG_ST_GY_CONNECTED;
        conf->status_flags |= SI_FLAG_ST_GY_READY;
    }
    else if (st->mpu_status == SI_MPU_FOUND) {
        conf->status_flags |= SI_FLAG_ST_GY_CONNECTED;
        conf->status_flags &= ~SI_FLAG_ST_GY_READY;
    }
    else {
        conf->status_flags &= ~SI_FLAG_ST_GY_CONNECTED;
        conf->status_flags &= ~SI_FLAG_ST_GY_READY;
    }

    if(conf->status_flags & SI_FLAG_ST_RESET_ORIENTATION){
        st->gyro_flags |= SI_FLAG_RESET_ORIENTATION;
        conf->status_flags &= ~(SI_FLAG_ST_RESET_ORIENTATION);
    }
}

void si_eth_pck_send(EthernetUDP* socket,
                     si_conf_t* conf,
                     si_device_state_t* st)
{
    conf->device_flags |= SI_FLAG_CFG_NO_REQ;
    si_eth_pck_respond(socket, conf, st);
}

void si_eth_pck_respond(EthernetUDP* socket,
                        si_conf_t* conf,
                        si_device_state_t* st)
{
    socket->beginPacket(st->sender_ip, st->sender_port);

    si_conf_packet_t pck;

    pck.preamble = SI_PACKET_PREAMBLE;
    pck.conf     = *conf;

    socket->write((uint8_t*) &pck, sizeof(si_conf_packet_t));

    socket->endPacket();

    conf->device_flags &= ~SI_FLAG_CFG_NO_REQ;
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

            bool reset = false;

            si_conf_packet_t* pck = (si_conf_packet_t*) buffer;
            si_conf_t* nconf      = &pck->conf;

            st->sender_ip   = socket->remoteIP();
            st->sender_port = socket->remotePort();

            if (!(SI_CONF_FLAG(nconf, SI_FLAG_CFG_REQ_ONLY))) {

                si_eth_conf_update(st, conf, pck);

                if (SI_CONF_FLAG(nconf, SI_FLAG_CFG_UPDATE)) si_eth_store(conf);

                st->sender_ip   = socket->remoteIP();
                st->sender_port = socket->remotePort();

                if (SI_CONF_FLAG(nconf, SI_FLAG_CFG_DEV_RESET)) reset = true;
            }

            si_eth_pck_respond(socket, conf, st);

            if (reset) si_eth_hwreset();
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