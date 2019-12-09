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

uint8_t buffer[256];

bool ledstate = false;

si_conf_t running_config;
si_device_state_t state;

EthernetUDP data_socket;
MPU6050 mpu;

void blink_callback(){
}

void setup()
{
    si_eth_hwprepare();

    Serial.begin(9600);
    pinMode(3, OUTPUT);
    pinMode(4, OUTPUT);
    pinMode(5, OUTPUT);

    delay(1000);

    si_eth_init(&data_socket, &running_config);
    si_gy_prepare(&state);
}

void loop()
{
    si_eth_run(&data_socket, &state, &running_config, buffer);
    si_gy_run(&mpu, &data_socket, &state, &running_config);
}