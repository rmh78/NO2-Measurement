#ifndef _datalogger_h_
#define _datalogger_h_

#ifdef __cplusplus
extern "C"{
#endif

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
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif