#ifndef _datalogger_h_
#define _datalogger_h_

#ifdef __cplusplus
extern "C"{
#endif

/* 
 * This class is responsible for handling the access to one file on the flash storage (SPIFFS)
 */
class DataLogger
{
private:
    const char * path;
public:
    DataLogger(const char * filePath);
    bool init();
    bool existsFile();
    bool appendFile(const char * message);
    void readFile();
    void deleteFile();
    void printInfo();
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif