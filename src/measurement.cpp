#include "measurement.h"

#include <Adafruit_SHT31.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>
#include <Adafruit_ADS1015.h>

#include <TinyGPS++.h> 
#include <Wire.h>

// Temperatur and Humidity sensor
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Pressure sensor
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

// analog digital converter
Adafruit_ADS1115 ads[2] = 
    {
        Adafruit_ADS1115(),
        Adafruit_ADS1115(0x49)
    };

// NO2 sensor constants
NO2Sensor sensors[2] = 
    { 
        NO2Sensor(202310057, 231, 225, 238, 234, 0.258), 
        NO2Sensor(202310055, 238, 233, 235, 220, 0.280) 
    };

NO2Sensor::NO2Sensor(uint32_t _serial_no, uint8_t _we_zero_electronic, uint8_t _we_zero_total, uint8_t _ae_zero_electronic, uint8_t _ae_zero_total, float _sensitivity)
{
    serial_no = _serial_no;
    we_zero_electronic = _we_zero_electronic;
    we_zero_total = _we_zero_total;
    ae_zero_electronic = _ae_zero_electronic;
    ae_zero_total = _ae_zero_total;
    sensitivity = _sensitivity;
}

// GPS
TinyGPSPlus gps;                            
HardwareSerial Serial1(1);

void EnvironmentData::lora_message(char* outStr) 
{
   /* example message:
        * +22
        * 32
        * 0955
        * 171209
        * 103612
        * 481597
        * 115319
        * 2401
        * 2406
        * 2370
        * 2375
        */

    uint16_t year = gps_year;
    if (year > 2000) 
    {
        year = year - 2000;
    }

    sprintf(outStr, "%+03.0f%02.0f%04.0f%02d%02d%02d%02d%02d%02d%06.0f%06.0f%04.0f%04.0f%04.0f%04.0f",
        sht31_temperature,
        sht31_humidity,
        bmp180_pressure,
        year,
        gps_month,
        gps_day,
        gps_hour,
        gps_minute,
        gps_second,
        gps_latitude * 10000,
        gps_longitude * 10000,
        no2_ae[0] * 10,
        no2_we[0] * 10,
        no2_ae[1] * 10,
        no2_we[1] * 10
    );
}

void EnvironmentData::logger_message(char* outStr) 
{
    sprintf(outStr, "%4d-%02d-%02d,%02d:%02d:%02d,%f,%f,%f,%f,%f,%f,%f,%f,%f\n",
        gps_year,
        gps_month,
        gps_day,
        gps_hour,
        gps_minute,
        gps_second,
        gps_latitude,
        gps_longitude,
        sht31_temperature,
        sht31_humidity,
        bmp180_pressure,
        no2_ae[0],
        no2_we[0],
        no2_ae[1],
        no2_we[1]
    );
}

void NO2Measurement::init() 
{
    // hardware serial for  GPS
    Serial.println("(I) - init GPS");
    Serial1.begin(9600, SERIAL_8N1, 17, 16);

    // init SHT31
    Serial.println("(I) - init SHT31");
    sht31.begin(0x44);
    sht31.heater(true);
    delay(2000);
    sht31.heater(false);

    // init BMP180
    Serial.println("(I) - init BMP180");
    bmp.begin();

    // init ADS1115
    Serial.println("(I) - init ADS1115");
    ads[0].setGain(GAIN_FOUR);
    ads[0].begin();
    ads[1].setGain(GAIN_FOUR);
    ads[1].begin();
}

void NO2Measurement::measure(EnvironmentData *data)
{
    readSHT31(data);
    readBMP085(data);
    readNO2(data);
}

void NO2Measurement::readSHT31(EnvironmentData *data) 
{
    uint16_t status = sht31.readStatus();

    float t = sht31.readTemperature();
    if (isnan(t)) 
    {
        Serial.println("***** I2C error ******");
        Wire.reset();
        //sht31.reset();
        t = sht31.readTemperature();
    }
    float h = sht31.readHumidity();

    data->sht31_temperature = t;
    data->sht31_humidity = h;

    if (loggingEnabled)
    {
        Serial.printf("(M) - SHT31 - temperature: %f, humidity: %f\n", t, h);
    }
}

void NO2Measurement::readBMP085(EnvironmentData *data) 
{
    sensors_event_t event;
    bmp.getEvent(&event);
    if (event.pressure)
    {
        float p = event.pressure;
        float t;
        bmp.getTemperature(&t);
        float seaLevelPressure = SENSORS_PRESSURE_SEALEVELHPA;
        float a = bmp.pressureToAltitude(seaLevelPressure, p, t);

        data->bmp180_temperature = t;
        data->bmp180_pressure = p;
        data->bmp180_altitude = a;

        if (loggingEnabled) 
        {
            Serial.printf("(M) - BMP180 - temperature: %f, pressure: %f, altitude: %f\n", t, p, a);
        }
    }
}

void NO2Measurement::readNO2(EnvironmentData *data) 
{
    for(int i = 0; i < 2; i++) 
    {
        // read NO2
        int readingsCount = 30;
        int readingsDelay = 1000;
        float ads_multiplier = 0.03125F;
        uint32_t acc_op1 = 0; // accumulator 1 value
        uint32_t acc_op2 = 0; // accumulator 2 value

        for (int j = 0; j < readingsCount; j++)
        {
            int16_t op1 = ads[i].readADC_Differential_0_1();    // Read ADC ports 0 and 1    
            int16_t op2 = ads[i].readADC_Differential_2_3();    // Read ADC ports 2 and 3

            acc_op1 += op1;
            acc_op2 += op2;

            delay(readingsDelay);
        }

        // averaged values for WE and Aux
        float op1 = (acc_op1 / readingsCount);
        float op2 = (acc_op2 / readingsCount);
        float we = op1 * ads_multiplier;
        float ae = op2 * ads_multiplier;

        // skip values greater than 999 because they do not fit into the lora-message
        if (we < 0 || we > 999 || ae < 0 || ae > 999) 
        {
            data->no2_we[i] = 0;
            data->no2_ae[i] = 0;
            data->no2_ppb[i] = 0;

            if (loggingEnabled) 
            {
                Serial.printf("(M) - SKIP NO2 (Sensor %d) - we: %f, ae: %f\n", i, ae, we);
            }
        }
        else 
        {
            // simple ppb calculation (see alphasense datasheet)
            float ppb = no2_algorithm_simple(sensors[i], we, ae);
            data->no2_we[i] = we;
            data->no2_ae[i] = ae;
            data->no2_ppb[i] = ppb;

            if (loggingEnabled) 
            {
                Serial.printf("(M) - NO2 (Sensor %d) - we: %f, ae: %f, ppb: %f\n", i, ae, we, ppb);
            }
        }
    }
}

float NO2Measurement::no2_algorithm_simple(NO2Sensor sensor, float we, float ae) 
{
    if (loggingEnabled) 
    {
        /*
        Serial.printf("(M) - NO2 calculation - we-zero: %d, ae-zero: %d, sensitivity: %f\n", 
            sensor.we_zero_total,
            sensor.ae_zero_total,
            sensor.sensitivity);
        */
    }

    float c = we - sensor.we_zero_total;
    if (c < 0) 
    {
        c = 0;
    }

    float e = ae - sensor.ae_zero_total;
    if (e < 0)
    {
        e = 0;
    }

    float f = c / sensor.sensitivity;
    float g = c - e;
    if (g < 0)
    {
        g = 0;
    }

    float h = g / sensor.sensitivity;

    return h;
}

void NO2Measurement::readGPS(EnvironmentData *data) 
{
  while (Serial1.available() > 0) 
  {
    if (gps.encode(Serial1.read())) 
    {
      if (gps.date.isValid())
      {
        data->gps_year = gps.date.year();
        data->gps_month = gps.date.month();
        data->gps_day = gps.date.day();
      }

      if (gps.time.isValid())
      {
        data->gps_hour = gps.time.hour();
        data->gps_minute = gps.time.minute();
        data->gps_second = gps.time.second();
      }

      if (gps.location.isValid()) 
      {
        data->gps_latitude = gps.location.lat();
        data->gps_longitude = gps.location.lng();
      }

      if (gps.altitude.isValid()) 
      {
        data->gps_altitude = gps.altitude.meters();
      }

      if (gps.satellites.isValid())
      {
        data->gps_satellites = gps.satellites.value();
      }

      if(gps.course.isValid()) 
      {
        data->gps_course = gps.course.deg();
      }

      if(gps.speed.isValid()) 
      {
        data->gps_speed = gps.speed.mph();
      }
    }
  }

    if (loggingEnabled) 
    {
        /*
        Serial.printf("(M) - GPS - date/time: %02d.%02d.%4d %02d:%02d:%02d\n", 
            data->gps_day, data->gps_month, data->gps_year,
            data->gps_hour, data->gps_minute, data->gps_second);
        Serial.printf("(M) - GPS - location: %f/%f\n", 
            data->gps_latitude, data->gps_longitude);
        */
    }
}