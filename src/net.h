#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>

#define SI_PACKET_PREAMBLE 0x3f39e3cc

#define SI_NET_RESET_PIN 6

typedef enum si_eth_status {
    SI_ETH_WAITING,
    SI_ETH_CONNECTED,
    SI_ETH_TRANSMITTING
} si_eth_status_t;

typedef enum si_mpu_status {
    SI_MPU_DISCONNECTED,
    SI_MPU_CONNECTED,
    SI_MPU_FOUND
} si_mpu_status_t;

typedef struct si_device_state {

    bool d4_state = false;
    bool d3_state = false;

    si_eth_status_t status;
    si_mpu_status_t mpu_status;

    uint16_t mpu_expected_packet_size = 16;
    uint16_t sender_port;

    uint64_t mpu_tmt;
    uint64_t mpu_sample_tmt;

    IPAddress sender_ip;

} si_device_state_t;

typedef struct si_config_data {

    uint8_t device_flags;
    uint8_t network_flags;
    uint8_t unused_space;
    uint8_t sampling_freq;

    uint32_t static_ipaddr;
    uint32_t static_subnet;

} si_conf_t;

typedef struct si_conf_packet {

    uint32_t preamble;
    si_conf_t conf;

} si_conf_packet_t;

typedef struct si_data_packet {

    uint32_t preamble;

        float w;
        float x;
        float y;
        float z;

} si_data_packet_t;

void si_eth_hwprepare();

void si_eth_hwreset();

/// initialize network devices
void si_eth_init(EthernetUDP* udp, si_conf_t* conf);

/// connect to network 
void si_eth_connect(si_conf_t* conf);

/// write configuration to eeprom
void si_eth_set(si_conf_t* p, si_conf_t* conf);

/// parse next packet
/// @return non zero if we should respond
uint8_t si_eth_pck_parse(EthernetUDP* socket, si_conf_t*, uint8_t* buffer);

void si_eth_send_pck(EthernetUDP* socket, si_device_state_t* st, si_data_packet_t* p);

/// respond with our current configuration
void si_eth_pck_respond(EthernetUDP* socket, si_conf_t*);

/// read data from socket
void si_eth_recv(EthernetUDP* socket, si_device_state_t*, si_conf_t* conf, uint8_t* buffer);

/// run networking routines 
void si_eth_run(EthernetUDP* socket, si_device_state_t*, si_conf_t* conf, uint8_t* buf);