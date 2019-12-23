#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>
#include <SPI.h>

#define SI_PACKET_PREAMBLE 0x3f39e3cc

#define SI_FLAG_CFG_UPDATE         _BV(0)
#define SI_FLAG_CFG_DEV_RESET      _BV(1)
#define SI_FLAG_CFG_STR_ENABLED    _BV(2)
#define SI_FLAG_CFG_GY_CALIBRATE   _BV(3)
#define SI_FLAG_CFG_GY_RESET_WORLD _BV(4)
#define SI_FLAG_CFG_NO_REQ         _BV(6)
#define SI_FLAG_CFG_REQ_ONLY       _BV(7)

#define SI_FLAG_NET_DHCP _BV(1)

#define SI_FLAG_ST_GY_CONNECTED _BV(0)
#define SI_FLAG_ST_GY_READY     _BV(1)

// clang-format off

#define SI_CONF_FLAG(conf_ptr, flag)   conf_ptr->device_flags & flag
#define SI_STATUS_FLAG(conf_ptr, flag) conf_ptr->status_flags & flag
#define SI_NET_FLAG(conf_ptr, flag)    conf_ptr->network_flags & flag

//clang-format on

#define SI_DV_STATUS_PIN 4
#define SI_NET_STATUS_PIN 3
#define SI_GY_STATUS_PIN 2
#define SI_DV_ST_LED _BV(0)
#define SI_NET_ST_LED _BV(1)
#define SI_GY_ST_LED _BV(2)

#define SI_LED_STATE(state, led) (state->led_states & led) == 1

#define SI_NET_RESET_PIN 6

#define SI_LED_FLIP(state, led) state->led_states ^= led

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

    uint8_t led_states = 0;

    si_eth_status_t status;
    si_mpu_status_t mpu_status;
    si_mpu_status_t last_mpu_status;

    uint16_t mpu_expected_packet_size = 16;
    uint16_t sender_port;

    int16_t filter_arr[16];
    int16_t* filter_write_head;

    uint64_t main_clk_tmt;
    uint64_t mpu_sample_tmt;
    uint64_t uint32_t;

    IPAddress sender_ip;

} si_device_state_t;

typedef struct si_config_data {

    uint8_t device_flags;
    uint8_t network_flags;
    uint8_t status_flags;
    uint8_t sampling_freq;

    uint32_t tgt_addr;
    uint16_t tgt_port;
    uint16_t pseq;

    uint32_t local_ip;
    uint32_t local_subnet;

} si_conf_t;

typedef struct si_conf_packet {

    uint32_t preamble;
    si_conf_t conf;

} si_conf_packet_t;

typedef struct si_data_packet {

    uint32_t preamble;
    uint16_t device_id;
    int16_t data[4];

} si_data_packet_t;

void si_eth_hwprepare();

void si_eth_hwreset();

/// initialize network devices
void si_eth_init(EthernetUDP* udp, si_conf_t* conf, char*, char*);

/// connect to network
void si_eth_connect(si_conf_t* conf);

/// write configuration to eeprom
void si_eth_store(si_conf_t* conf);

/// parse next packet
/// @return non zero if we should respond
uint8_t si_eth_pck_parse(EthernetUDP* socket, si_conf_t* conf, uint8_t* buffer);

void si_eth_send_pck(EthernetUDP* socket,
                     si_conf_t* conf,
                     si_device_state_t* st,
                     si_data_packet_t* p);

void si_eth_conf_update(si_device_state_t*, si_conf_packet_t*, si_conf_t*);

void si_eth_conf_update(si_device_state_t* st,
                        si_conf_t* conf);

void si_eth_pck_send(EthernetUDP* socket,
                     si_conf_t* conf,
                     si_device_state_t* st);

/// respond with our current configuration
void si_eth_pck_respond(EthernetUDP* socket, si_conf_t*, si_device_state_t*);

/// read data from socket
void si_eth_recv(EthernetUDP* socket,
                 si_device_state_t*,
                 si_conf_t* conf,
                 uint8_t* buffer);

/// run networking routines
void si_eth_run(EthernetUDP* socket,
                si_device_state_t*,
                si_conf_t* conf,
                uint8_t* buf);