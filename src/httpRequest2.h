#ifndef httpRequest2_h
#define httpRequest2_h
#include <Arduino.h>
#include <Client.h>
#include <SdFat.h>

class http {
  public:
    enum DOSstatus {GOOD, BAD, TERRIBLE};
    unsigned long hmaptime[64];
    byte amt[64];
    uint32_t hmapips[64] = {0};
    class SdFat32 SD;
};

class httpRequest {
  public:
    Client& client;
    class http& srv;
    char path[64]; // the path requested
    char method[16]; // the http request method (GET, POST, PUT, etc)
    byte pathlen; // length of path, used to optimize later function calls
    byte methodlen; // length of method
    long contentLength; // used when recieving request
    http::DOSstatus isDOS(IPAddress ip);
    bool pathIs(const char*const ispath);
    bool methodIs(const char*const ismethod);
    bool pathStartsWith(const char*const str);
    bool isFile();
    void sendStatus(const char*const msg);
    bool getRequest(const unsigned long maxtimeout);
    void stop();
    void recieveSaveFileAndSendOK(const unsigned int buffsize, const unsigned long maxBytes, const unsigned long timeout, const unsigned long maxtimeout);
    void sendFileAndHeader(const unsigned int buffsize, const unsigned long maxtimeout);
    void updateLeaderboard(); // update highscore leaderboard
    inline httpRequest(Client& cl, http& s): client(cl), srv(s) {contentLength = -1;}
};

#endif