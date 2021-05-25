#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
#define ETH_PHY_POWER 12

#include <ETH.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <WS2812FX.h>
#include <AsyncTCP.h>

#include "ESP32_RMT_Driver.h"
#include "nymealight.h"

static bool eth_connected = false;
static const int tcpPort = 1080;

NymeaLight *lightStrip1 = nullptr;
NymeaLight *lightStrip2 = nullptr;

AsyncServer* tcpServer = nullptr;

static void handleError(void* arg, AsyncClient* client, int8_t error) 
{
  Serial.printf("\n connection error %s from client %s \n", client->errorToString(error), client->remoteIP().toString().c_str());
}

static void handleData(void* arg, AsyncClient* client, void *data, size_t len) 
{
  Serial.printf("\nReceived data from %s; length: %i \n", client->remoteIP().toString().c_str(), len);
  Serial.write((uint8_t*)data, len);

  NymeaLight::Status status = lightStrip1->processData((uint8_t*)data, len);
  lightStrip2->processData((uint8_t*)data, len);

  if (status == NymeaLight::StatusInvalidPlayload) {
    Serial.println("Status: invalid payload");
  } else if  (status == NymeaLight::StatusInvalidCommand) {
    Serial.println("Status: invalid command");
  }
  
  // reply to client
  if (client->space() > 4 && client->canSend()) {
    char reply[4];
    reply[0] = static_cast<uint8_t *>(data)[0]; //command
    reply[1] = static_cast<uint8_t *>(data)[1]; //requestId
    reply[2] = status;
    reply[3] = '\n';
    client->add(reply, 4);
    Serial.println("Sending response");
    Serial.write((uint8_t*)reply, 4);
    client->send();
  } else {
    Serial.println("Could not send response");
  }
}

static void handleDisconnect(void* arg, AsyncClient* client) 
{
  Serial.printf("\n client %s disconnected \n", client->remoteIP().toString().c_str());
}

static void handleTimeOut(void* arg, AsyncClient* client, uint32_t time) 
{
  Serial.printf("\n client ACK timeout ip: %s \n", client->remoteIP().toString().c_str());
}

static void handleNewClient(void* arg, AsyncClient* client) 
{
  Serial.printf("\n new client has been connected to server, ip: %s", client->remoteIP().toString().c_str());
  
  // register events
  client->onData(&handleData, NULL);
  client->onError(&handleError, NULL);
  client->onDisconnect(&handleDisconnect, NULL);
  client->onTimeout(&handleTimeOut, NULL);

  // notify client
  if (client->space() > 2 && client->canSend()) {
    char reply[2];
    reply[0] = NymeaLight::Notification::NotificationReady;
    reply[1] = 0x01; //notification id;
    client->add(reply, 2);
    client->send();
  }
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("nymea-light");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting");
  WiFi.onEvent(WiFiEvent);
  delay(10);
  ETH.begin();

  if (!MDNS.begin("nymea-light")) {
      Serial.println("Error setting up MDNS responder!");
      while (1) {
        delay(1000);
      }
  }
  // Add service to MDNS-SD
  MDNS.addService("nymealight", "tcp", tcpPort);
  MDNS.addServiceTxt("nymealight", "tcp", "manufacturer", "nymea GmbH");
  MDNS.addServiceTxt("nymealight", "tcp", "name", "Garage innen");
  MDNS.addServiceTxt("nymealight", "tcp", "mac", ETH.macAddress());
  
  Serial.println("mDNS responder started");

  lightStrip1 = new NymeaLight(new WS2812FX(600, 32, NEO_GRB  + NEO_KHZ800));
  lightStrip2 = new NymeaLight(new WS2812FX(600, 33, NEO_GRB  + NEO_KHZ800));
  rmt_tx_int(RMT_CHANNEL_0, lightStrip1->strip()->getPin());
  rmt_tx_int(RMT_CHANNEL_1, lightStrip2->strip()->getPin());
  lightStrip1->init();
  lightStrip2->init();

  tcpServer = new AsyncServer(tcpPort);
  tcpServer->onClient(&handleNewClient, tcpServer);
  tcpServer->begin();
  Serial.println("TCP server started");
}


void loop()
{
  lightStrip1->process();
  lightStrip2->process();
}

void myCustomShow1(void) {
  uint8_t *pixels = lightStrip1->strip()->getPixels();
  uint16_t numBytes = lightStrip1->strip()->getNumBytes() + 1;
  rmt_write_sample(RMT_CHANNEL_0, pixels, numBytes, false);
}

void myCustomShow2(void) {
  uint8_t *pixels = lightStrip2->strip()->getPixels();
  uint16_t numBytes = lightStrip2->strip()->getNumBytes() + 1;
  rmt_write_sample(RMT_CHANNEL_1, pixels, numBytes, false);
}
