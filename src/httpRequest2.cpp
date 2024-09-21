#include <httpRequest2.h>
#include <Arduino.h>
#include <Client.h>
#include <SdFat.h>

// httpRequest class
bool httpRequest::pathIs(const char*const ispath) {
  return (strlen(ispath)!=pathlen)?false:(memcmp(path, ispath, pathlen)==0); // should be faster than a simple strcmp and should be as secure as strncmp
}
bool httpRequest::methodIs(const char*const ismethod) {
  return (strlen(ismethod)!=methodlen)?false:(memcmp(method, ismethod, methodlen)==0);
}
bool httpRequest::pathStartsWith(const char*const str) {
  const word sl = strlen(str);
  return (sl>pathlen)?false:(memcmp(path, str, sl)==0); // same trick used in pathIs
}
void httpRequest::sendStatus(const char*const msg) {
  client.print("HTTP/1.1 ");
  client.print(msg);
  client.print("\r\nConnection: closed\r\n\r\n");
  stop();
}
void httpRequest::stop() {
  client.flush();
  client.stop();
}
bool httpRequest::isFile() {
  const char*const ext = strrchr(path, '.'); // find extension
  return (ext==nullptr)?false:(strrchr(path, '/')<ext); // if extension is found before last directory, then it is not a file. same when the extension doesn't exist
}

bool httpRequest::getRequest(const unsigned long maxtimeout) {
  const unsigned long mt = millis(); // used to limit time spent on request
  client.setTimeout(100);
  bool buffer = false; // start by reading in the method
  char* bufferPointer = method-1; // accounts for prefix increment during loop
  char* bufferMax = bufferPointer+sizeof(method);  // accounts for null character
  methodlen = 0;
  // read in method, then path
  while((millis()-mt)<=maxtimeout) {
    if(!client.available()) {
      if(!client.connected()) break; // handle disconnect
      continue;
    }
    const char ch = client.read();
    if(ch!=' ') {
      // continue reading into the buffer, if there isn't space return
      if(++bufferPointer>=bufferMax) {
        sendStatus((buffer)?"414 URI Too Long":"405 Method Not Allowed"); // stop buffer overflows
        return false;
      }
      *bufferPointer = ch;
      continue;
    }
    bufferPointer[1] = '\0'; // terminate string
    if(buffer==false) {
      methodlen = 1+bufferPointer-method;
      // switch to reading the path
      client.read(); // ignores the first '/' in the path
      buffer = true;
      bufferPointer = path-1;
      bufferMax = bufferPointer+sizeof(path);
      continue;
    }
    if(*(bufferPointer) == '/') *(bufferPointer--) = '\0'; // remove trailing '/'
    pathlen = 1+(bufferPointer-path); // get path length
    if(methodlen==3) {
      if(strcmp(method, "GET") == 0) { // if get request
        if(pathlen!=0) return true; // if not index, just return vaild now
        strncpy(path, "index.htm", sizeof(path) - 1); // if index, replace with index.htm, then return valid
        pathlen = 9;
        return true;
      }
    }
    
    if(!client.find("Content-Length: ")) {
      sendStatus("411 Length Required");
      return false;
    }
    contentLength = client.parseInt();
    if(contentLength<0) { // invalid length
      sendStatus("400 Bad Request");
      return false;
    }
    // look for blank line that marks end of headers
    if(!client.find("\r\n\r\n")) { // no ending found in reasonable time
      sendStatus("431 Request Header Fields Too Large");
      return false;
    }
    return true;
    
  }
  // client disconnected
  stop(); // client is gone, don't bother sending status
  return false;
}

// http class
// sends a file at a path, to the client (and very fast)
void httpRequest::sendFileAndHeader(const unsigned int buffsize, const unsigned long maxtimeout) {
  const unsigned long mt = millis(); // used to limit time spent on request
  class File32 file = srv.SD.open(path, O_RDONLY);
  if(!file) return sendStatus("500 Internal Server Error");
  // send headers
  client.print("HTTP/1.1 200 OK\r\nContent-Length: ");
  client.print(file.size());
  client.print("\r\nContent-Type: "); // then print the content type, by most common to least common
  const char*const ext = strrchr(path, '.')+1; // get the extension
  if     (strcmp_P(ext, "htm" )==0) client.print("text/html");
  else if(strcmp_P(ext, "ico" )==0) client.print("image/vnd.microsoft.icon");
  else if(strcmp_P(ext, "txt" )==0) client.print("text/plain");
  else if(strcmp_P(ext, "json")==0) client.print("application/json");
  else                              client.print("application/octet-stream"); // if unknown, treat it as a binary file
  client.print("\r\nConnection: closed\r\n\r\n");
  byte fileBuffer[buffsize]; // initialize file buffer
  while(client.connected() && ((millis()-mt)<=maxtimeout)) { // while there are bytes that we can read and while the client is connected
    const signed short bytesToRead = file.read(fileBuffer, buffsize); // read bytes, save the amount of bytes read in bytesToRead
    if(bytesToRead<=0) break; // if no more bytes to read, return
    client.write(fileBuffer, bytesToRead); // send bytes, using bytesToRead as the amount
  }
  // file sent, or client disconnected during the transmission
  file.close();
  return stop();
}

void httpRequest::updateLeaderboard() {
  char username[17] = {'\0'};
  client.read((uint8_t*)username, 16);
  for(int i=0; i<16; i++) if(username[i]==' ') username[i] = '\0';
  int score = (int)(client.parseInt());
  if(!client.connected() && !client.available()) return;
  if(score<=0) return sendStatus("200 OK"); // scores of zero are to be discarded early
  // note: add code to allow for removal of inappropriate names without server shutdown
  File32 file = srv.SD.open("highscores.txt", O_RDONLY);
  if(!file) return sendStatus("500 Internal Server Error");
  char entries[10][32] = {'\0'};
  int numentries = 0;
  for(int i=0; (i<10) && (file.available()>0); i++) {
    file.read(&entries[i][0], 32);
    for(int j=0; j<32; j++) if(entries[i][j]==' ' || entries[i][j]=='\n') entries[i][j] = '\0';
    numentries = i+1;
  }
  file.close();
  // remove the user's previous score
  for(int i=0; i<numentries;) {
    if(memcmp(username, &entries[i][0], 16)==0) {
      const int entryScore = atoi(&entries[i][16]);
      if(entryScore>score) score = entryScore;
      for(int j=i; j<numentries-1; j++) memcpy(&entries[j][0], &entries[j+1][0], 32);
      numentries--;
    } else i++;
  }
  for(int i=0; i<10; i++) {
    int entryScore = 0;
    if(i<numentries) entryScore = atoi(&entries[i][16]); // gets score from current entry, if it is valid
    if(score>entryScore) {
      numentries+=(numentries<10);
      // move existing entries down the list if needed
      for(int j=numentries-1; j>i; j--) memcpy(&entries[j][0], &entries[j-1][0], 32);
      // write new entry
      memset(&entries[i][0], '\0', 32);
      memcpy(&entries[i][0], username, 16);
      itoa(score, &entries[i][16], 10);
      // remove null characters and use spaces instead
      for(int i=0; i<numentries; i++) {
        for(int j=0; j<31; j++) if(entries[i][j]=='\0') entries[i][j] = ' ';
        entries[i][31] = '\n';
      }
      // save the file
      file = srv.SD.open("highscores.txt", O_WRONLY | O_TRUNC);
      if(!file) return sendStatus("500 Internal Server Error");
      file.truncate();
      file.write(&entries[0][0], 32*numentries);
      file.flush();
      file.close();
      return sendStatus("201 Created"); // return this if the leaderboard updated
    }
  }
  return sendStatus("200 OK");
}

void httpRequest::recieveSaveFileAndSendOK(const unsigned int buffsize, const unsigned long maxBytes, const unsigned long timeout, const unsigned long maxtimeout) {
  // create path to save file using rand()
  const char hexarr[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'}; // path in hexadecimal
  const unsigned int rngval = (unsigned int)rand();
  path[0] = 's';
  path[1] = 'a';
  path[2] = 'v';
  path[3] = 'e';
  path[4] = 's';
  path[5] = '/';
  path[6]  = hexarr[(rngval    )&0xF];
  path[7]  = hexarr[(rngval>>4 )&0xF];
  path[8]  = hexarr[(rngval>>8 )&0xF];
  path[9]  = hexarr[(rngval>>12)&0xF];
  path[10] = hexarr[(rngval>>16)&0xF];
  path[11] = hexarr[(rngval>>20)&0xF];
  path[12] = hexarr[(rngval>>24)&0xF];
  path[13] = hexarr[(rngval>>28)&0xF];
  path[14] = '.';
  path[15] = 'j';
  path[16] = 's';
  path[17] = 'o';
  path[18] = 'n';
  path[19] = '\0';
  File32 file = srv.SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if(!file) return sendStatus("500 Internal Server Error");
  file.truncate();
  byte fileBuffer[buffsize]; // initialize file buffer
  signed int bytesToRead = buffsize;
  unsigned long time = millis(); // used for end of file detection
  const unsigned long mt = millis(); // used to limit time spent on request
  unsigned long numBytes = 0;
  do { // while there are bytes that we can read and while the client is connected
    if(!client.available()) continue;
    do {
      bytesToRead = client.read(fileBuffer, buffsize); // read bytes, save the amount of bytes read in bytesToRead
      file.write(fileBuffer, bytesToRead);                // send bytes, using bytesToRead as the amount
      // if too many bytes recieved, end the connection
      if((numBytes+=bytesToRead)>maxBytes) {
        file.flush();
        file.truncate();
        file.close();
        return sendStatus("413 Payload Too Large"); // no large files allowed
      }
    } while(client.available());
    time = millis();
  } while(client.connected() && ((millis()-time)<=timeout) && (millis()-mt)<=maxtimeout && bytesToRead<contentLength); // stop slow requests
  if((millis()-mt)>maxtimeout) { // for timed out requests
    file.truncate();
    file.flush();
    file.close();
    return sendStatus("408 Request Timeout");
  }
  file.flush();
  file.close();
  client.print("HTTP/1.1 201 Created\r\nConnection: closed\r\n\r\n"); // always say it is a new file, even if it isn't for security reasons
  client.print(path); // tell client where savegame is stored for later retrieval
  return stop();
}

http::DOSstatus httpRequest::isDOS(IPAddress ip) {
  //return http::GOOD;
  const uint32_t byteip = (uint32_t)ip;
  const byte hash = byteip&63;
  if(srv.hmapips[hash]!=byteip) { // if not same IP, then reset timer and allocate slot to client IP
    srv.hmaptime[hash] = millis();
    srv.hmapips[hash] = ip;
    srv.amt[hash] = 0;
    return http::GOOD;
  } else if((signed long)(millis()-srv.hmaptime[hash])>2048) { // if greater than timeout, then we don't care
    srv.hmaptime[hash] = millis();
    srv.amt[hash] = 0;
    return http::GOOD;
  }
  srv.amt[hash]++;
  if(srv.amt[hash]>32) {
    srv.amt[hash] = 32;
    srv.hmaptime[hash] = millis()+8192; // add more time if they send way too many requests to slow them down more
    return http::TERRIBLE;
  } else if(srv.amt[hash]>16) {
    srv.hmaptime[hash] = millis();
    return http::BAD;
  } else return http::GOOD;
}