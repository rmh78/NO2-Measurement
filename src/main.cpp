#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <U8x8lib.h>

#include "measurement.h"
#include "datalogger.h"
#include "lorawan-node.h"

/*
 * TOGGLE_MODE means that only measurement or sending is active
 * mode can be toggled with the button on the ESP32.
 * default mode is measuring. sending can be activated with the button.
 * after sending all messages, the mode switches back to measuring.
 */
//#define TOGGLE_MODE

/* OFFLINE_MODE means that data is written to sd-card instead 
 * of sending it via lorawan
 */
#define OFFLINE_MODE

/* LMIC callback methods to get the ids. 
 * The APPEUI, DEVEUI and APPKEY are defined in the file lorawan-node.h
 * Rename the file lorawan-node.h.example to lorawan-node.h
 * and paste in the node ids.
 */
void os_getArtEui (uint8_t* buf) { memcpy_P(buf, APPEUI, 8);}
void os_getDevEui (uint8_t* buf) { memcpy_P(buf, DEVEUI, 8);}
void os_getDevKey (uint8_t* buf) {  memcpy_P(buf, APPKEY, 16);}

/* LIMIC pin mapping
 * For usage with the Heltec ESP32 Lora Board
 */
const lmic_pinmap lmic_pins = {
  .nss = 18,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 14,
  .dio = {26, 33, 32},
};
static osjob_t sendjob;

/* Queue to store the measurement data
 */
QueueHandle_t xQueue;
const TickType_t xTicksToWait = pdMS_TO_TICKS(100);

/* Measurement variables
 * The wait periods for measurement and sending are defined here
 */
NO2Measurement no2;
EnvironmentData currentData;
int measurementWaitPeriod = 10 * 60 * 1000;
int sendingWaitPeriod = 15 * 1000;
unsigned long lastMeasurement = millis() - measurementWaitPeriod;
unsigned long lastSending = millis() - sendingWaitPeriod;

/* OLED
 * Used to display the measurement and processing data 
 */
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

/* Toggle button
 * Used only in TOGGLE_MODE to switch from measurement to sending mode
 */
const int buttonPin = 0;     // the number of the pushbutton pin
const int ledPin =  25;      // the number of the LED pin
static void readToggleButton();
static void setToggleButton(bool value);
static int oldButtonState = HIGH;
static bool toggleOn = false;

/* Data logger
 * Used to store the measurement data into a csv on the micro SD card
 */
DataLogger dataLogger = DataLogger("/no2-data.csv");

/* Prototypes */
void sendStart(osjob_t* j);
void measureAndSend(osjob_t* j);
void measure();
void send();
void messageSent(bool removeFromQueue);
void displayGPS(EnvironmentData *data);
void displayData(EnvironmentData *data);
void displayQueue();

void setup() {
    Serial.begin(9600);
    delay(1000);
    Serial.println(F("Starting"));

    Serial.println("-- init OLED");
    pinMode(16,OUTPUT);
    digitalWrite(16, LOW);   // turn the LED on (HIGH is the voltage level)
    delay(100);              // wait for a second
    digitalWrite(16, HIGH);  // turn the LED off by making the voltage LOW
    delay(1000); 
    u8x8.begin();
    u8x8.setPowerSave(0);
    u8x8.setFont(u8x8_font_victoriamedium8_r);
    u8x8.clear();
    u8x8.println("oled - ok");

    no2.init();
    u8x8.println("sensors - ok");

    Serial.println("-- init button and led");
    pinMode(ledPin, OUTPUT);
    pinMode(buttonPin, INPUT);
    u8x8.println("button/led - ok");

    Serial.println("-- init queue");
    xQueue = xQueueCreate(1000, sizeof(EnvironmentData));
    u8x8.println("queue - ok");

    #ifdef OFFLINE_MODE
        Serial.println("-- init data logger");
        delay(1000);
        if (dataLogger.init()) 
        {
            u8x8.println("sdcard - ok");
            // comment out in production
            //dataLogger.deleteFile();

            if (!dataLogger.existsFile()) 
            {
                Serial.println("-- write csv-header");
                dataLogger.appendFile("date,time,latitude,longitude,temperature,humidity,pressure,ae0,we0,ae1,we1\n");
            }
        }
        else 
        {
            u8x8.println("sdcard - err");
            return;
        }
    #endif
    
    #ifndef OFFLINE_MODE
        // LMIC init
        os_init();
        LMIC_reset();
    #endif

    delay(5000);
    u8x8.clear();

    os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(1), measureAndSend);
}

void loop() 
{
    // Let LMIC handle background tasks
    os_runloop_once();
}

void measureAndSend(osjob_t* j) 
{
    no2.readGPS(&currentData);
    displayGPS(&currentData);

    #ifdef TOGGLE_MODE
        if (toggleOn) {
            // only send data if toggle button is on
            send();
            if (uxQueueMessagesWaiting(xQueue) == 0) 
            {
                // switch to measure mode if no more messages to send
                setToggleButton(false);
            }
        } else {
            // only measure if toggle button is off
            measure();
            os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(1), measureAndSend);
        }
    #endif

    #ifndef TOGGLE_MODE
        measure();
        send();
    #endif
}

void measure()
{
    // check if it is time for measurement
    unsigned long elapsedTime = millis() - lastMeasurement;
    if (elapsedTime > measurementWaitPeriod) 
    {
        lastMeasurement = millis();

        Serial.println("(M) ================================");
        Serial.println("(M) - start measurement");

        u8x8.clearLine(7);
        u8x8.setCursor(0, 7);
        u8x8.printf("measuring");

        no2.measure(&currentData);
        displayData(&currentData);

        u8x8.clearLine(7);

        if (xQueueSend(xQueue, &currentData, xTicksToWait))
        {
            Serial.printf("(M) - added message to queue (waiting: %d, free: %d)\n", 
                uxQueueMessagesWaiting(xQueue),
                uxQueueSpacesAvailable(xQueue));
            displayQueue();
        }
    }
    else 
    {
        #ifdef TOGGLE_MODE
            readToggleButton();
        #endif

        int remainingSeconds = (measurementWaitPeriod - elapsedTime) / 1000;
        u8x8.setCursor(0, 7);
        u8x8.printf("next %03d", remainingSeconds);
    }
}
 
void send()
{
    // check if it is time for sending
    unsigned long elapsedTime = millis() - lastSending;
    EnvironmentData data;
    if(elapsedTime > sendingWaitPeriod && xQueuePeek(xQueue, &data, xTicksToWait))
    {
        lastSending = millis();

        #ifdef OFFLINE_MODE
            Serial.println("(S) ================================");
            Serial.println("(S) - start sd-card writing");

            // append data to file on sd-card
            char message[200];
            currentData.sdcard_message(message);
            u8x8.clearLine(7);
            u8x8.setCursor(0, 7);
            if (dataLogger.appendFile(message)) 
            {
                u8x8.printf("append - ok");
            }
            else 
            {
                u8x8.printf("append - err");
            }
            delay(2000);
            messageSent(true);
        #endif

        #ifndef OFFLINE_MODE
            Serial.println("(S) ================================");
            Serial.println("(S) - start sending");

            u8x8.clearLine(7);
            u8x8.setCursor(0, 7);
            u8x8.printf("sending");

            // get lora message formatted
            char message[100];
            data.lora_message(message);
            Serial.printf("(S) - message: %s\n", message);

            // convert char-array to int-array
            uint8_t lmic_data[strlen(message)];
            for(int idx = 0; idx < strlen(message); idx++)
            {
                lmic_data[idx] = (uint8_t)message[idx];
            }
            Serial.printf("(S) - message size: %d\n", sizeof(lmic_data));
            
            // sending data via lorawan
            LMIC_setTxData2(1, lmic_data, sizeof(lmic_data), 1);
        #endif
    }
    else 
    {
        // nothing to send - go into next cycle
        os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(1), measureAndSend);
    }
}

void onEvent (ev_t ev) 
{
    Serial.print("(S) - ");
    Serial.print(os_getTime());
    Serial.print(": ");
    switch(ev) {
        case EV_SCAN_TIMEOUT:
            Serial.println(F("EV_SCAN_TIMEOUT"));
            break;
        case EV_BEACON_FOUND:
            Serial.println(F("EV_BEACON_FOUND"));
            break;
        case EV_BEACON_MISSED:
            Serial.println(F("EV_BEACON_MISSED"));
            break;
        case EV_BEACON_TRACKED:
            Serial.println(F("EV_BEACON_TRACKED"));
            break;
        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;
        case EV_JOINED:
            Serial.println(F("EV_JOINED"));

            // Disable link check validation (automatically enabled
            // during join, but not supported by TTN at this time).
            LMIC_setLinkCheckMode(0);
            break;
        case EV_RFU1:
            Serial.println(F("EV_RFU1"));
            break;
        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;
        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;
        case EV_TXSTART:
            Serial.println(F("EV_TXSTART"));
            break;
        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
            if (LMIC.txrxFlags & TXRX_ACK) 
            {
                Serial.println(F("Received ack"));
                messageSent(true);
            }
            else 
            {
                messageSent(false);
            }

            break;
        case EV_LOST_TSYNC:
            Serial.println(F("EV_LOST_TSYNC"));
            break;
        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;
        case EV_RXCOMPLETE:
            // data received in ping slot
            Serial.println(F("EV_RXCOMPLETE"));
            break;
        case EV_LINK_DEAD:
            Serial.println(F("EV_LINK_DEAD"));
            break;
        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;
         default:
            Serial.println(F("Unknown event"));
            Serial.println(ev);
            break;
    }
}

void messageSent(bool removeFromQueue)
{
    u8x8.clearLine(7);

    // if sending was successful remove the message from the queue
    if (removeFromQueue && uxQueueMessagesWaiting(xQueue) > 0) 
    {
        EnvironmentData data;
        if (xQueueReceive(xQueue, &data, xTicksToWait)) 
        {
            Serial.printf("(S) - removed message from queue (waiting: %d, free: %d)\n", 
                uxQueueMessagesWaiting(xQueue), 
                uxQueueSpacesAvailable(xQueue));
            displayQueue();
        }
    }

    // trigger the next measurement
    os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(1), measureAndSend);
}

void displayGPS(EnvironmentData *data) 
{
    // date and time
    u8x8.setCursor(0, 0);
    u8x8.printf("%02d.%02d.", data->gps_day, data->gps_month);
    u8x8.setCursor(7, 0);
    u8x8.printf("%02d:%02d:%02d", data->gps_hour, data->gps_minute, data->gps_second);
    
    // latitude and longitude
    u8x8.setCursor(0, 1);
    u8x8.printf("%.4f,%.4f", data->gps_latitude, data->gps_longitude);
}

void displayData(EnvironmentData *data)
{
    // SHT31 temperature and humidity
    u8x8.setCursor(0, 2);
    u8x8.printf("T %2.2f H %2.2f", data->sht31_temperature, data->sht31_humidity);

    // BMP180 temperature and pressure
    u8x8.setCursor(0, 3);
    u8x8.printf("T %2.2f P %6.2f", data->bmp180_temperature, data->bmp180_pressure);

    // NO2
    u8x8.clearLine(4);
    u8x8.setCursor(0, 4);
    u8x8.printf("NO2 %.0f/%.0f/%.0f", data->no2_ae[0], data->no2_we[0], data->no2_ppb[0]);
    u8x8.clearLine(5);
    u8x8.setCursor(0, 5);
    u8x8.printf("NO2 %.0f/%.0f/%.0f", data->no2_ae[1], data->no2_we[1], data->no2_ppb[1]);
}

void displayQueue() 
{
    u8x8.setCursor(0, 6);
    u8x8.printf("queue %03d", uxQueueMessagesWaiting(xQueue));
}

static void readToggleButton() 
{
    // Get the current state of the button
    int newButtonState = digitalRead(buttonPin);

    // Has the button gone high since we last read it?
    if (newButtonState == LOW && oldButtonState == HIGH) 
    {
        setToggleButton(!toggleOn);
    }

    // Store the button's state so we can tell if it's changed next time round
    oldButtonState = newButtonState;    
}

static void setToggleButton(bool value) 
{
    if (value) 
    {
        // Toggle on
        digitalWrite(ledPin, HIGH);
    } 
    else 
    {
        // Toggle off
        digitalWrite(ledPin, LOW);
    }
    toggleOn = value;
}