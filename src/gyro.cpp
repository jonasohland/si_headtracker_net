#define SI_DETAIL
#include "gyro.h"
#undef SI_DETAIL

void si_gy_prepare(si_device_state_t* st)
{
    pinMode(2, OUTPUT);
    st->mpu_tmt        = millis();
    st->mpu_sample_tmt = millis();

    Fastwire::setup(100, true);
    I2Cdev::readTimeout = 50;
}

void si_gyro_check(MPU6050* mpu, si_device_state_t* st)
{
    uint8_t buf[6];
    buf[0] = 0;

    uint8_t ret = I2Cdev::readBytes(MPU6050_DEFAULT_ADDRESS,
                                    MPU6050_RA_WHO_AM_I,
                                    MPU6050_WHO_AM_I_LENGTH,
                                    buf);

    if (ret < 1 || buf[0] != 104)
        st->mpu_status = SI_MPU_DISCONNECTED;
    else {
        if (st->mpu_status == SI_MPU_DISCONNECTED)
            st->mpu_status = SI_MPU_FOUND;
    }

    digitalWrite(2, st->mpu_status != SI_MPU_DISCONNECTED);
}

void si_gyro_init(MPU6050* mpu, si_device_state_t* st)
{
    digitalWrite(2, LOW);

    mpu->initialize();

    if (!mpu->dmpInitialize()) {

        st->mpu_expected_packet_size = mpu->dmpGetFIFOPacketSize();
        st->mpu_status               = SI_MPU_CONNECTED;

        mpu->setDMPEnabled(true);

        digitalWrite(2, HIGH);
    }
}

void si_sample(MPU6050* mpu,
               EthernetUDP* sock,
               si_device_state_t* st,
               si_conf_t* conf)
{
    uint8_t buf[64];
    Quaternion q;

    int mpuIntStatus = mpu->getIntStatus();
    int fifocnt      = mpu->getFIFOCount();

    if (mpuIntStatus & _BV(0) && fifocnt >= 16) {
        
        si_data_packet_t pck;

        mpu->getFIFOBytes(buf, st->mpu_expected_packet_size);
        mpu->dmpGetQuaternion((Quaternion*) &pck.w, buf);
        mpu->resetFIFO();

        si_eth_send_pck(sock, st, &pck);
    }
}

void si_gy_run(MPU6050* mpu,
               EthernetUDP* sock,
               si_device_state_t* st,
               si_conf_t* conf)
{
    if (millis() - st->mpu_tmt > 1000 / 2) {

        si_gyro_check(mpu, st);

        if (st->mpu_status == SI_MPU_FOUND) si_gyro_init(mpu, st);

        st->mpu_tmt  = millis();
        st->d3_state = !st->d3_state;

        digitalWrite(3, st->d3_state);
    }

    if (st->mpu_status == SI_MPU_CONNECTED && conf->device_flags & _BV(2)) {

        if (millis() - st->mpu_sample_tmt > 1000 / 25) {

            si_sample(mpu, sock, st, conf);

            st->mpu_sample_tmt = millis();
            st->d4_state       = !st->d4_state;

            digitalWrite(4, st->d4_state);
        }
    }
}