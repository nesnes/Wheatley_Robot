#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <Servo.h>
#include <WebSocketsServer.h>

#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

StaticJsonBuffer<10000> jsonBuffer;
        
extern "C" {
#include "user_interface.h"
}

#define D0 16
#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14
#define D6 12
#define D7 13
#define D8 15
#define DTX 1
#define RX 3

#define SERVO_COUNT 5
int SERVO_PINS[SERVO_COUNT] = {D1,D2,D5,D6,D7};
Servo servos[SERVO_COUNT];
int SERVO_POS_DEFAULT[SERVO_COUNT] = {88,90,100,76,95};
//0: Rotate on himself, G=180, M=90, D=0
//1: eye up/down, haut=83, bas=98, mid=88
//2: eye close/open, fermé=100, ouvert=80, plissés=95
//3: eye rotate, G=60, M=76, D=90
//4: body up/down, H=130, M=95 ,B=82
//5: eye LED

#define EYE_LED_PIN D4

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);    // create a websocket server on port 81

float animFreq = 20;//Hz
bool animLoop = false;

String getContentType(String filename);
bool handleFileRead(String path);

#define lerp(t, a, b) ( a + t * (b - a) )

struct AnimationKey {
  int servoId;
  int origin; //angle 0-255
  int target; //angle 0-255
  int startTime; //ms
  int endTime; //ms
  bool done;

  AnimationKey(int _servoId, int _origin, int _target, int _startTime, int _endTime)
  : servoId(_servoId)
  , origin(_origin)
  , target(_target)
  , startTime(_startTime)
  , endTime(_endTime)
  , done(false)
  {
      
  }

  bool isReady(int currentTime){
    if(origin==target)
      return currentTime >= startTime && !done;
    else
      return currentTime >= startTime;
  }

  bool isDone(int currentTime){
    if(origin==target)
      return done;
    else
      return currentTime >= endTime && done;
  }
  
  int getValue(int currentTime){
    if(currentTime >= endTime || origin==target){
      done = true;
      return target;
    }
    else{
      float progress = currentTime - startTime;
      float duration = endTime - startTime;
      return lerp( (progress/duration), (float)origin, (float)target);
    }
  }

  void reset(){
    done = false;
  }
};

AnimationKey* currentAnimation = NULL;
int currentAnimationSize = 0;
long currentAnimationStartTime = 0;
bool stopAnimation = false;

void log(char* msg, int length=0){
  if(length==0)
    length=strlen(msg);
  webSocket.broadcastTXT(msg, length);  
}

IPAddress staticIp(192,168,42,100);
IPAddress staticGW(192,168,42,1);
IPAddress staticMask(255,255,255,0);
bool ropeInit=false;
void setup() {
  //Init Serial
  Serial.begin(74880);
  Serial.println();

  //Init Servos
  for(int i=0;i<SERVO_COUNT;i++){
    if(i==0){
      pinMode(SERVO_PINS[i], OUTPUT);
      digitalWrite(SERVO_PINS[i], LOW);
      continue; //don't init rope climber
    }
    servos[i].attach(SERVO_PINS[i]);
    servos[i].write(SERVO_POS_DEFAULT[i]);
  }

  //Init Eye led
  pinMode(EYE_LED_PIN, OUTPUT);
  digitalWrite(EYE_LED_PIN, HIGH);

  //WIFI
  if(1){ // Create hotspot
    //Init Wifi AP
    WiFi.softAP("Wheatley", "azertyuiop");
  }
  else{ // Connect to existing network
    WiFi.config(staticIp,staticGW,staticMask);
    WiFi.begin("NetworkName","azertyuiop");
    while(WiFi.status() != WL_CONNECTED){
      delay(250);
      digitalWrite(EYE_LED_PIN, HIGH);
      delay(250);
      digitalWrite(EYE_LED_PIN, LOW);
    }
  }
  digitalWrite(EYE_LED_PIN, LOW);
  
  SPIFFS.begin();

  //Init web server
  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      server.send(404, "text/plain", "404: Not Found");
  });
  server.begin();

  //Init Websocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  Serial.println("WebSocket server started.");

  //Init OTA
  ArduinoOTA.setHostname("Wheatley OTA");
  ArduinoOTA.setPassword((const char *)"azertyuiop");
  ArduinoOTA.onStart([]() {
    Serial.println("Start OTA");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd OTA");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("OTA End Failed");
  });
  ArduinoOTA.begin();
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

void showRAM(){
  char msg[100];
  sprintf(msg, "RAM: %i\0", system_get_free_heap_size());
  log(msg);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        //resetRobot();                  // Turn rainbow off when a new connection is established
      }
      break;
    case WStype_TEXT:                     // if new text data is received
      Serial.printf("[%u] get Text: %s\n", num, (char*)payload);
      if(!strncmp("setServo", (char*)payload, 8)){
        int servoId = 0;
        int angle = 90;
        sscanf((char*)payload, "setServo %i %i", &servoId, &angle);
        if(servoId==5){
            digitalWrite(EYE_LED_PIN, angle);
        }
        else{
          if(servoId==0 && !ropeInit){ // init rope on first action
              servos[0].attach(SERVO_PINS[0]);
              servos[0].write(SERVO_POS_DEFAULT[0]);
              ropeInit=true;
          }
          servoId = min(SERVO_COUNT, max(0,servoId));
          angle = min(255, max(0,angle));
          servos[servoId].write(angle);
        }
      }
      if(!strncmp("stop", (char*)payload, 4)){
        stopAnimation = true;
      }
      if(!strncmp("{", (char*)payload, 1)){ //Json document
        stopAnimation = false;
        jsonBuffer.clear();
        JsonObject& root = jsonBuffer.parseObject((char*)payload);
        if(!root.success() || !root.containsKey("animation")) {
          log("Invalid JSON\n");
          return;
        }
        if(currentAnimation != NULL) {
          log("Already playing\n");
          return;
        }
        animLoop = false;
        if(root.containsKey("loop"))
          animLoop = root["loop"];
        animFreq = root["frequency"];
        if(animFreq<=0)
          animFreq = 50;
        JsonArray& animation = root["animation"];
        currentAnimation = (AnimationKey*)malloc(animation.size() * sizeof(AnimationKey));
        int i=0;
        currentAnimationSize = animation.size(); 
        for (JsonObject& elem : animation) {
          /*JsonObject& servoIdCheck = elem["servoId"];
          if(!servoIdCheck.success()){
            currentAnimationSize--;
            continue;
          }*/
          
          currentAnimation[i] = AnimationKey(
            elem["servoId"],
            elem["origin"],
            elem["target"],
            elem["startTime"],
            elem["endTime"]
          );
          i++;
        }
      }
      break;
  }
}

int lastAnimTime = 0;
void loop() {
  webSocket.loop();
  server.handleClient();
  ArduinoOTA.handle();
  delay(10);

  float animWaitTarget = 1000.f/animFreq;
  int timeElapsed = millis()-lastAnimTime;
  if(timeElapsed>=animWaitTarget){
    lastAnimTime = millis();
    /* Play Animation */
    if(currentAnimationSize>0 && currentAnimation != NULL){
      if(stopAnimation){
        free(currentAnimation);
        currentAnimation = NULL;
        currentAnimationSize = 0;
        currentAnimationStartTime = 0;
        log("Animation Stopped");
      }
      else {
        log("Animation Playing");
        long _millis = millis();
        if(currentAnimationStartTime == 0)
          currentAnimationStartTime = _millis;
        int currentTime = _millis - currentAnimationStartTime;
  
        //Find AnimationKey to play
        int maxTime = 0;
        for(int i=0;i<currentAnimationSize && !stopAnimation;i++){
          AnimationKey& key  = currentAnimation[i];
          maxTime = max(maxTime, key.endTime);
          if(key.isReady(currentTime) && !key.isDone(currentTime)){
            if(key.servoId==5){
              digitalWrite(EYE_LED_PIN, key.getValue(currentTime));
            }
            else{
              if(key.servoId==0 && !ropeInit){ // init rope on first action
                servos[0].attach(SERVO_PINS[0]);
                servos[0].write(SERVO_POS_DEFAULT[0]);
                ropeInit=true;
              }
              int servoId = min(SERVO_COUNT, max(0,key.servoId));
              int angle = min(180, max(0,key.getValue(currentTime)));
              servos[servoId].write(angle);
              char msg[50];
              //sprintf(msg, "S:%i A:%i, T:%i\0", servoId, angle, currentTime);
              //log(msg);
            }
          }        
        }
        if(currentTime > maxTime)
          if(animLoop){
            currentAnimationStartTime = 0;
            for(int i=0;i<currentAnimationSize && !stopAnimation;i++){
              AnimationKey& key  = currentAnimation[i];
              key.reset();
            }
          }
          else {
            stopAnimation = true;
          }
      }
    }
  }
}
