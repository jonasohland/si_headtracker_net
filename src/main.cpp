// clang-format off
#define DEBUG
#define I2CDEV_IMPLEMENTATION I2CDEV_BUILTIN_FASTWIRE

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>
#include <EEPROM.h>
// clang-format on

#include "net.h"
#include "gyro.h"

char bonjour_hostname[11] = "si_htrk_00";
char bonjour_servicename[17] = "si_htrk_00._htrk";


uint8_t buffer[256];

bool ledstate = false;

si_conf_t running_config;
si_device_state_t state;

EthernetUDP data_socket;
MPU6050 mpu;

void setup()
{
    si_eth_hwprepare();

    delay(2000);

    Serial.begin(9600);

    pinMode(SI_DV_STATUS_PIN, OUTPUT);
    pinMode(SI_NET_STATUS_PIN, OUTPUT);
    pinMode(SI_GY_STATUS_PIN, OUTPUT);

    digitalWrite(SI_DV_STATUS_PIN, HIGH);

    delay(1000);

    si_eth_init(&data_socket, &running_config, bonjour_hostname, bonjour_servicename);
    si_gy_prepare(&state);
}

void loop()
{
    si_eth_run(&data_socket, &state, &running_config, buffer);
    si_gy_run(&mpu, &data_socket, &state, &running_config);
}