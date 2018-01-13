/*
 * ----------------------------------------------------------------------------
 * NO2 measurement with ESP32 and LoRaWan
 * https://github.com/rmh78/NO2-Measurement
 * ----------------------------------------------------------------------------
 */


#ifndef _measurement_h_
#define _measurement_h_

#include <Arduino.h>

#ifdef __cplusplus
extern "C"{
#endif

/* 
 * This class holds all measurement data of one measurement iteration
 */
class EnvironmentData
{
public:
    float     sht31_temperature = 0;
    float     sht31_humidity = 0;
    float     bmp180_temperature = 0;
    float     bmp180_pressure = 0;
    float     bmp180_altitude = 0;
    uint8_t   gps_day = 99;
    uint8_t   gps_month = 99;
    uint16_t  gps_year = 9999;
    uint8_t   gps_hour = 99;
    uint8_t   gps_minute = 99;
    uint8_t   gps_second = 99;
    double    gps_latitude = 0;
    double    gps_longitude = 0;
    double    gps_altitude = 0;
    uint32_t  gps_satellites = 0;
    double    gps_course = 0;
    double    gps_speed = 0;
    float     no2_ae[2];
    float     no2_we[2];
    float     no2_ppb[2];

    void lora_message(char* outStr);
    void logger_message(char* outStr);
};

/* 
 * This class holds the zero values and sensitivity of a alphasense NO2 sensor (the values are shipped with the sensor) 
 */
class NO2Sensor
{
public:
    uint32_t serial_no;
    uint8_t we_zero_electronic;
    uint8_t we_zero_total;
    uint8_t ae_zero_electronic;
    uint8_t ae_zero_total;
    float sensitivity;
    NO2Sensor(uint32_t _serial_no, uint8_t _we_zero_electronic, uint8_t _we_zero_total, uint8_t _ae_zero_electronic, uint8_t _ae_zero_total, float _sensitivity);
};

/* 
 * This class is responsible for the measurement of temperature, humidity, pressure, NO2 and GPS
 */
class NO2Measurement
{
public:
    void init();
    void measure(EnvironmentData *data);
    void readGPS(EnvironmentData *data);
private:
    bool loggingEnabled = true;
    void readSHT31(EnvironmentData *data);
    void readBMP085(EnvironmentData *data);
    void readNO2(EnvironmentData *data);
    float no2_algorithm_simple(NO2Sensor sensor, float we, float ae);
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif