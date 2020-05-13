// clang-format off
#define DEBUG

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetBonjour.h>
#include <EEPROM.h>
#include "shared/serial.h"
// clang-format on

#include "net.h"
// #include "gyro.h"

struct Quat {
    float w;
    float x;
    float y;
    float z;
};

char bonjour_hostname[11] = "si_htrk_00";
char bonjour_servicename[17] = "si_htrk_00._htrk";

uint8_t buffer[256];

bool ledstate = false;

si_conf_t running_config;
si_device_state_t state;
si_serial_t serial;

EthernetUDP data_socket;
// MPU6050 mpu;

void handle_value(si_gy_values_t value, uint8_t* data) 
{
    Serial.print("Received complete message: ");
    Serial.println(value);

    if(value == SI_GY_SRATE) {
        Serial.print("SRATE: ");
        Serial.println(*data);
    } else if(value == SI_GY_QUATERNION) {
        Serial.print("QUAT: ");
        Serial.print(((Quat*) data)->w);
        Serial.print(" ");
        Serial.print(((Quat*) data)->x);
        Serial.print(" ");
        Serial.print(((Quat*) data)->y);
        Serial.print(" ");
        Serial.println(((Quat*) data)->z);
    }
}

void setup()
{
    si_eth_hwprepare();

    delay(2000);

    pinMode(1, OUTPUT);
    pinMode(0, INPUT);

    Serial.begin(9600);

    si_serial_init(&Serial1, &serial, nullptr, {});

    pinMode(SI_DV_STATUS_PIN, OUTPUT);
    pinMode(SI_NET_STATUS_PIN, OUTPUT);
    pinMode(SI_GY_STATUS_PIN, OUTPUT);
    pinMode(SI_NET_RESET_CONF_PIN, INPUT_PULLUP);

    digitalWrite(SI_DV_STATUS_PIN, HIGH);
    digitalWrite(3, HIGH);

    delay(1000);

    si_eth_init(&data_socket, &running_config, bonjour_hostname, bonjour_servicename);
    
    // si_gy_prepare(&state);
}

void loop()
{
    si_eth_run(&data_socket, &state, &running_config, buffer);
    si_serial_run(&serial);
}