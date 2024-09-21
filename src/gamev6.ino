#include <Ethernet.h>
#include <SdFat.h>
#include <SPI.h>
#include <httpRequest2.h>
byte mac[6] = {0xA8,0x61,0x0A,0xAE,0xA9,0x8B};
//byte ip[4] = {10,132,9,80};
byte ip[4] = {192,168,1,177};
class EthernetServer server(80);
class http WebServer;

void setup() {
  Serial.begin(9600);
  unsigned int rngseed = 0;
  rngseed ^= analogRead(A0); // use floating pins for better rng
  rngseed<<=8;
  rngseed ^= analogRead(A1);
  rngseed<<=8;
  rngseed ^= analogRead(A2);
  rngseed<<=8;
  rngseed ^= analogRead(A3);
  srand((unsigned int)rngseed);
  pinMode(10, OUTPUT);
  digitalWrite(10, true); // disable ethernet chip while initializing SdFat
  WebServer.SD.begin(4, 24000000UL); // 24 MHZ SPI go brrr
  Ethernet.init(10);
  Ethernet.begin(mac); // initialize without dhcp to save ram and performance
  Ethernet.setRetransmissionTimeout(100);
  Ethernet.setRetransmissionCount(4);
  server.begin();
  Serial.println(Ethernet.localIP());
}

void loop() {
  rand(); // to make calls to random more random and unpredictable (so next calls of rand() cannot be predicted reliably)
  class EthernetClient client = server.accept();
  if(!client) {
    Ethernet.maintain();
    return;
  }
  if(!client.connected()) return;
  class httpRequest request(client, WebServer);
  switch(request.isDOS(client.remoteIP())) {
    case(http::GOOD): return handleRequest(request); // good client
    case(http::BAD):  return request.sendStatus("429 Too Many Requests"); // I herby declare Denial Of Service, denied
    default:          return request.stop(); // really bad clients only
  }
}

void handleRequest(httpRequest& request) {
  if(!request.getRequest(1024ul)) return;
  if(request.methodIs("GET")) {
    if(!(WebServer.SD.exists(request.path)) || !(request.isFile())) return request.sendStatus("404 Not Found"); // don't send directories for extra security
    return request.sendFileAndHeader(1024, 1024ul);
  } else if(request.methodIs("POST")) {
    if(request.pathIs("savegame")) return request.recieveSaveFileAndSendOK(1024, 65536ul, 64ul, 256ul); // note: should later read the content length header to avoid waiting once the file is recieved
    if(request.pathIs("highscore")) return request.updateLeaderboard();
    return request.sendStatus("403 Forbidden"); // only allow savegame requests to go through
  } else if(request.methodIs("BREW")) return request.sendStatus("418 I'm a teapot"); // I can't brew coffee, man I'm a teapot...
  else return request.sendStatus("405 Method Not Allowed");
}