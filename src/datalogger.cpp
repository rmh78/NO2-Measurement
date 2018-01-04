#include "datalogger.h"

#include "FS.h"
#include "SPIFFS.h"

DataLogger::DataLogger(const char * filePath) 
{
    path = filePath;
}

bool DataLogger::init() 
{
    if(!SPIFFS.begin()) 
    {
        Serial.println("(S) - SPIFFS Mount Failed");
        return false;
    }

    return true;
}

bool DataLogger::existsFile() 
{
    return SPIFFS.exists(path);
}

bool DataLogger::appendFile(const char * message) 
{
    Serial.printf("(S) - SPIFFS Appending to file: %s\n", path);

    File file = SPIFFS.open(path, FILE_APPEND);
    if(!file){
        Serial.println("(S) - SPIFFS Failed to open file for appending");
        return false;
    }
    if(file.print(message)){
        Serial.printf("(S) - SPIFFS Message appended (file-size: %d bytes)\n", file.size());
        file.close();
        return true;
    } else {
        Serial.printf("(S) - SPIFFS Append failed (file-size: %d bytes)\n", file.size());
        file.close();
        return false;
    }
}

void DataLogger::readFile() 
{
    Serial.printf("(S) - SPIFFS Reading file: %s\n", path);

    File file = SPIFFS.open(path);
    if(!file){
        Serial.println("(S) - SPIFFS Failed to open file for reading");
        return;
    }

    Serial.println("(S) - SPIFFS Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}

void DataLogger::deleteFile()
{
    Serial.printf("(S) - SPIFFS Deleting file: %s\n", path);
    if(SPIFFS.remove(path)){
        Serial.println("(S) - SPIFFS File deleted");
    } else {
        Serial.println("(S) - SPIFFS Delete failed");
    }
}

void DataLogger::printInfo() 
{
    Serial.printf("(S) - SPIFFS memory used/total bytes: %d/%d\n", SPIFFS.usedBytes(), SPIFFS.totalBytes());
}