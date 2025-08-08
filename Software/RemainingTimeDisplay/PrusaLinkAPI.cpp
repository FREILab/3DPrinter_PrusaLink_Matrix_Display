/* ___       _        ____       _       _      _    ____ ___
  / _ \  ___| |_ ___ |  _ \ _ __(_)_ __ | |_   / \  |  _ \_ _|
 | | | |/ __| __/ _ \| |_) | '__| | '_ \| __| / _ \ | |_) | |
 | |_| | (__| || (_) |  __/| |  | | | | | |_ / ___ \|  __/| |
  \___/ \___|\__\___/|_|   |_|  |_|_| |_|\__/_/   \_\_|  |___|
.......By Stephen Ludgate https://www.chunkymedia.co.uk.......
.......Redesigned for Prusa Link by Marius Tetard 
....... 08/2025....

*/

#include "PrusaLinkAPI.h"
#include "Arduino.h"

PrusaLinkApi::PrusaLinkApi(void){
  if (_debug)
    Serial.println("Be sure to Call init to setup and start the PrusaLinkApi instance");
}

PrusaLinkApi::PrusaLinkApi(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, String apiKey) {
  init(client, prusaLinkIp, prusaLinkPort, apiKey);
}

void PrusaLinkApi::init(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, String apiKey) {
  _client         = &client;
  _apiKey         = apiKey;
  _prusaLinkIp    = prusaLinkIp;
  _prusaLinkPort  = prusaLinkPort;
  _usingIpAddress = true;
}

PrusaLinkApi::PrusaLinkApi(Client &client, char *prusaLinkUrl, int prusaLinkPort, String apiKey) {
  init(client, prusaLinkUrl, prusaLinkPort, apiKey);
}

void PrusaLinkApi::init(Client &client, char *prusaLinkUrl, int prusaLinkPort, String apiKey) {
  _client         = &client;
  _apiKey         = apiKey;
  _prusaLinkUrl   = prusaLinkUrl;
  _prusaLinkPort  = prusaLinkPort;
  _usingIpAddress = false;
}

String PrusaLinkApi::sendRequestToPrusaLink(String type, String command, const char *data) {
  if (_debug)
    Serial.println("PrusaLinkApi::sendRequestToPrusaLink() CALLED");

  if ((type != "GET") && (type != "POST") && (type != "DELETE")) {
    if (_debug)
      Serial.println("PrusaLinkApi::sendRequestToPrusaLink() Only GET, POST & DELETE are supported... exiting.");
    return "";
  }

  String statusCode       = "";
  String headers          = "";
  String body             = "";
  bool finishedStatusCode = false;
  bool finishedHeaders    = false;
  bool currentLineIsBlank = true;
  int ch_count            = 0;
  int headerCount         = 0;
  int headerLineStart     = 0;
  int bodySize            = -1;
  unsigned long now;

  bool connected;

  if (_usingIpAddress)
    connected = _client->connect(_prusaLinkIp, _prusaLinkPort);
  else
    connected = _client->connect(_prusaLinkUrl, _prusaLinkPort);

  if (connected) {
    if (_debug)
      Serial.println(".... connected to server");

    char useragent[64];
    snprintf(useragent, 64, "User-Agent: %s", USER_AGENT);

    _client->println(type + " " + command + " HTTP/1.1");
    _client->print("Host: ");
    if (_usingIpAddress)
      _client->println(_prusaLinkIp);
    else
      _client->println(_prusaLinkUrl);
    _client->print("X-Api-Key: ");
    _client->println(_apiKey);
    _client->println(useragent);
    _client->println("Connection: close");
    if (data != NULL) {
      _client->println("Content-Type: application/json");
      _client->print("Content-Length: ");
      _client->println(strlen(data));
      _client->println();
      _client->println(data);
    } else {
      _client->println();
    }

    now = millis();
    while (millis() - now < PLAPI_TIMEOUT) {
      while (_client->available()) {
        char c = _client->read();

        if (_debug)
          Serial.print(c);

        if (!finishedStatusCode) {
          if (c == '\n')
            finishedStatusCode = true;
          else
            statusCode = statusCode + c;
        }

        if (!finishedHeaders) {
          if (c == '\n') {
            if (currentLineIsBlank)
              finishedHeaders = true;
            else {
              if (headers.substring(headerLineStart).startsWith("Content-Length: "))
                bodySize = (headers.substring(headerLineStart + 16)).toInt();
              headers = headers + c;
              headerCount++;
              headerLineStart = headerCount;
            }
          } else {
            headers = headers + c;
            headerCount++;
          }
        } else {
          if (ch_count < maxMessageLength) {
            body = body + c;
            ch_count++;
            if (ch_count == bodySize)
              break;
          }
        }
        if (c == '\n')
          currentLineIsBlank = true;
        else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
      if (ch_count == bodySize && bodySize != -1)
        break;
    }
  } else {
    if (_debug) {
      Serial.println("connection failed");
    }
  }

  closeClient();

  int httpCode = extractHttpCode(statusCode, body);
  if (_debug) {
    Serial.print("\nhttpCode:");
    Serial.println(httpCode);
  }
  httpStatusCode = httpCode;
  if(httpStatusCode < 200 || httpStatusCode >= 300){httpErrorBody = body;}

  return body;
}

String PrusaLinkApi::sendGetToPrusaLink(String endpoint) {
  if (_debug)
    Serial.println("PrusaLinkApi::sendGetToPrusaLink() CALLED");

  return sendRequestToPrusaLink("GET", endpoint, NULL);
}

String PrusaLinkApi::sendPostToPrusaLink(String endpoint, const char *postData) {
  if (_debug)
    Serial.println("PrusaLinkApi::sendPostToPrusaLink() CALLED");
  return sendRequestToPrusaLink("POST", endpoint, postData);
}

String PrusaLinkApi::sendDeleteToPrusaLink(String endpoint) {
    if (_debug)
    Serial.println("PrusaLinkApi::sendDeleteToPrusaLink() CALLED");
  return sendRequestToPrusaLink("DELETE", endpoint, NULL);
}


bool PrusaLinkApi::getPrinterStatus() {
  String response = sendGetToPrusaLink("/api/v1/status");

  StaticJsonDocument<JSONDOCUMENT_SIZE> root;
  DeserializationError error = deserializeJson(root, response);

  if (error) {
    if(_debug) Serial.println("Failed to parse printer status");
    return false;
  }

  printerStats.printerState = (const char*)root["printer"]["state"];
  printerStats.printerStatePrinting = (strcmp(printerStats.printerState.c_str(), "PRINTING") == 0);
  printerStats.printerStatePaused = (strcmp(printerStats.printerState.c_str(), "PAUSED") == 0);
  printerStats.printerStateError = (strcmp(printerStats.printerState.c_str(), "ERROR") == 0) || (strcmp(printerStats.printerState.c_str(), "ATTENTION") == 0);
  printerStats.printerStateFinished = (strcmp(printerStats.printerState.c_str(), "FINISHED") == 0);
  printerStats.printerStateReady = (strcmp(printerStats.printerState.c_str(), "IDLE") == 0);
  printerStats.printerStateBusy = (strcmp(printerStats.printerState.c_str(), "BUSY") == 0);

  printerStats.printerBedTempActual = root["printer"]["temp_bed"];
  printerStats.printerBedTempTarget = root["printer"]["target_bed"];

  printerStats.printerTool0TempActual = root["printer"]["temp_nozzle"];
  printerStats.printerTool0TempTarget = root["printer"]["target_nozzle"];

  return true;
}

bool PrusaLinkApi::getJobInfo() {
  String response = sendGetToPrusaLink("/api/v1/job");

  StaticJsonDocument<JSONDOCUMENT_SIZE> root;
  DeserializationError error = deserializeJson(root, response);

  if (error || !root.containsKey("progress")) {
      if(_debug && error) Serial.println("Failed to parse job info");
      if(_debug && !root.containsKey("progress")) Serial.println("No active job");
      return false;
  }

  jobInfo.jobFileName = (const char*)root["file"]["display_name"];
  jobInfo.progressCompletion = root["progress"]["completion"];
  jobInfo.progressPrintTime = root["progress"]["print_time"];
  jobInfo.progressPrintTimeLeft = root["progress"]["print_time_left"];

  return true;
}

bool PrusaLinkApi::printerCommand(const char* gcodeCommand) {
    char postData[POSTDATA_GCODE_SIZE];
    snprintf(postData, POSTDATA_GCODE_SIZE, "{\"command\": \"%s\"}", gcodeCommand);
    sendPostToPrusaLink("/api/v1/printer/command", postData);
    return (httpStatusCode == 204);
}

bool PrusaLinkApi::printerHome(bool x, bool y, bool z) {
    return printerCommand("G28");
}

bool PrusaLinkApi::jobStart() {
    sendPostToPrusaLink("/api/v1/job", "{\"command\": \"start\"}");
    return (httpStatusCode == 204);
}

bool PrusaLinkApi::jobPause() {
    sendPostToPrusaLink("/api/v1/job", "{\"command\": \"pause\"}");
    return (httpStatusCode == 204);
}

bool PrusaLinkApi::jobResume() {
    sendPostToPrusaLink("/api/v1/job", "{\"command\": \"resume\"}");
    return (httpStatusCode == 204);
}

bool PrusaLinkApi::jobStop() {
    sendDeleteToPrusaLink("/api/v1/job");
    return (httpStatusCode == 204);
}


void PrusaLinkApi::closeClient() { _client->stop(); }

int PrusaLinkApi::extractHttpCode(String statusCode, String body) {
  if (_debug) {
    Serial.print("\nStatus code to extract: ");
    Serial.println(statusCode);
  }
  int firstSpace = statusCode.indexOf(" ");
  int lastSpace  = statusCode.lastIndexOf(" ");
  if (firstSpace > -1 && lastSpace > -1 && firstSpace != lastSpace) {
    String statusCodeExtract = statusCode.substring(firstSpace + 1, lastSpace);
    int statusCodeInt        = statusCodeExtract.toInt();
    if (_debug and statusCodeInt != 200 and statusCodeInt != 201 and statusCodeInt != 202 and statusCodeInt != 204) {
      Serial.print("\nSERVER RESPONSE CODE: " + statusCode);
      if (body != "")
        Serial.println(" - " + body);
      else
        Serial.println();
    }
    return statusCodeInt;
  } else
    return -1;
}