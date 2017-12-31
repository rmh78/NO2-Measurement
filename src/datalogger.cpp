#include "datalogger.h"

#include "FS.h"
#include "SD.h"
#include "SPI.h"

DataLogger::DataLogger(const char * filePath) 
{
    path = filePath;
}

bool DataLogger::init() 
{
    int retries = 0;
    while(!SD.begin()) {
        if (retries > 10) {
            Serial.println("Card Mount Failed");
            return false;
        }
        retries++;
    }
    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return false;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    return true;
}

bool DataLogger::existsFile() 
{
    return SD.exists(path);
}

bool DataLogger::appendFile(const char * message) 
{
    Serial.printf("Appending to file: %s\n", path);

    File file = SD.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return false;
    }
    if(file.print(message)){
        Serial.println("Message appended");
        file.close();
        return true;
    } else {
        Serial.println("Append failed");
        file.close();
        return false;
    }
}

void DataLogger::readFile() 
{
    Serial.printf("Reading file: %s\n", path);

    File file = SD.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}

void DataLogger::deleteFile()
{
    Serial.printf("Deleting file: %s\n", path);
    if(SD.remove(path)){
        Serial.println("File deleted");
    } else {
        Serial.println("Delete failed");
    }
}