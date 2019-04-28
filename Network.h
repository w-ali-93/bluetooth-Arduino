#ifndef Network_h
#define Network_h

#include "Arduino.h"
#include <string.h>
#include <stdint.h>
#include "Storage.h"

#define DOWNLOAD_FILE 2
#define UPLOAD_FILE 3
#define READY 7

class Network
{
public:
    // Public getter for Display instance
    static Network& instance()
    {
        static Network network;
        return network;
    }
    // Prohibit copying of Display class
    Network(Network const&) = delete;
    void operator=(Network const&) = delete;
    // File download and upload functions
    void downloadFile(char filename[], bool push);
    void uploadFile(char filename[], bool push);
private:
    Network();
};

#endif
