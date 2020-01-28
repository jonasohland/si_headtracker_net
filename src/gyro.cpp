#define SI_DETAIL
#include "gyro.h"
#undef SI_DETAIL

bool si_filter_run(si_device_state_t* st, si_conf_t* conf)
{
    return true;
}

void si_gy_prepare(si_device_state_t* st)
{
    st->main_clk_tmt   = millis();
    st->mpu_sample_tmt = millis();

    st->mpu_status = SI_MPU_DISCONNECTED;

    Fastwire::setup(100, true);
    I2Cdev::readTimeout = 50;
}

void si_gyro_check(MPU6050* mpu, si_conf_t* conf, si_device_state_t* st)
{
    uint8_t buf[MPU6050_WHO_AM_I_LENGTH];
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

    if (!(SI_CONF_FLAG(conf, SI_FLAG_CFG_STR_ENABLED))
        || st->mpu_status == SI_MPU_DISCONNECTED)
        digitalWrite(SI_GY_STATUS_PIN, st->mpu_status != SI_MPU_DISCONNECTED);
}

void si_gyro_init(MPU6050* mpu, si_conf_t* conf, si_device_state_t* st)
{
    digitalWrite(2, LOW);

    mpu->initialize();

    if (!mpu->dmpInitialize()) {

        st->mpu_expected_packet_size = mpu->dmpGetFIFOPacketSize();
        st->mpu_status               = SI_MPU_CONNECTED;

        mpu->setDMPEnabled(true);
    }
}

void si_sample(MPU6050* mpu,
               EthernetUDP* sock,
               si_device_state_t* st,
               si_conf_t* conf)
{
    uint8_t buf[64];

    int mpuIntStatus = mpu->getIntStatus();
    int fifocnt      = mpu->getFIFOCount();

    if (mpuIntStatus & _BV(0) && fifocnt >= 16) {

        si_data_packet_t pck;

        mpu->getFIFOBytes(buf, st->mpu_expected_packet_size);
        mpu->dmpGetQuaternion(&pck.w, buf);
        mpu->resetFIFO();

        if(st->gyro_flags & SI_FLAG_RESET_ORIENTATION){

            Serial.println("oreset");

            Quaternion current_rot(pck.w / 16384.f, pck.x / 16384.f, pck.y / 16384.f, pck.z / 16384.f);

            current_rot = current_rot.getConjugate();

            memcpy(&st->offset, &current_rot, sizeof(Quaternion)); 

            st->gyro_flags |= SI_FLAG_APPLY_OFFSETS;
            st->gyro_flags &= ~(SI_FLAG_RESET_ORIENTATION);
        }

        if(st->gyro_flags & SI_FLAG_APPLY_OFFSETS){

            Quaternion current_rot(pck.w / 16384.f, pck.x / 16384.f, pck.y / 16384.f, pck.z / 16384.f);

            Quaternion nq = current_rot.getProduct(*((Quaternion*) &st->offset));

            pck.w = nq.w * 16384;
            pck.x = nq.x * 16384;
            pck.y = nq.y * 16384;
            pck.z = nq.z * 16384;
        }

        if(conf->status_flags & SI_FLAG_ST_INVERT_X)
            pck.x = -pck.x;

        if(conf->status_flags & SI_FLAG_ST_INVERT_Y)
            pck.y = -pck.y;

        if(conf->status_flags & SI_FLAG_ST_INVERT_Z)
            pck.z = -pck.z;

        si_eth_send_pck(sock, conf, st, &pck);
    }
}

void si_gy_run(MPU6050* mpu,
               EthernetUDP* sock,
               si_device_state_t* st,
               si_conf_t* conf)
{
    if (millis() - st->main_clk_tmt > 1000 / 2) {

        si_gyro_check(mpu, conf, st);

        if (st->mpu_status != st->last_mpu_status) {

            si_eth_conf_update(st, conf);
            si_eth_pck_send(sock, conf, st);

            st->last_mpu_status = st->mpu_status;
        }

        if (st->mpu_status == SI_MPU_FOUND) si_gyro_init(mpu, conf, st);

        st->led_states ^= SI_DV_ST_LED;
        digitalWrite(SI_DV_STATUS_PIN, (st->led_states & SI_DV_ST_LED));

        st->main_clk_tmt = millis();
    }

    if (st->mpu_status == SI_MPU_CONNECTED
        && SI_CONF_FLAG(conf, SI_FLAG_CFG_STR_ENABLED)) {

        if (millis() - st->mpu_sample_tmt > 1000 / conf->sampling_freq) {

            st->mpu_sample_tmt = millis();
            si_sample(mpu, sock, st, conf);

            st->led_states ^= SI_GY_ST_LED;
            digitalWrite(SI_GY_STATUS_PIN, (st->led_states & SI_GY_ST_LED));
        }
    }
}