/* ___       _        ____       _       _      _    ____ ___
  / _ \  ___| |_ ___ |  _ \ _ __(_)_ __ | |_   / \  |  _ \_ _|
 | | | |/ __| __/ _ \| |_) | '__| | '_ \| __| / _ \ | |_) | |
 | |_| | (__| || (_) |  __/| |  | | | | | |_ / ___ \|  __/| |
  \___/ \___|\__\___/|_|   |_|  |_|_| |_|\__/_/   \_\_|  |___|
.......By Stephen Ludgate https://www.chunkymedia.co.uk.......
.......Redesigned for Prusa Link by Marius Tetard
.......Updated for API Key Auth....... 08/2025....

*/

#include "PrusaLinkAPI.h"
#include "Arduino.h"

// The Base64 helper function is no longer needed and has been removed.

PrusaLinkApi::PrusaLinkApi(void){
	if (_debug)
		Serial.println("Be sure to Call init to setup and start the PrusaLinkApi instance");
}

PrusaLinkApi::PrusaLinkApi(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, const char* apiKey) {
  init(client, prusaLinkIp, prusaLinkPort, apiKey);
}

void PrusaLinkApi::init(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, const char* apiKey) {
  _client         = &client;
  _prusaLinkIp    = prusaLinkIp;
  _prusaLinkPort  = prusaLinkPort;
  _usingIpAddress = true;
  _apiKey         = apiKey; // Store the API Key
}

PrusaLinkApi::PrusaLinkApi(Client &client, char *prusaLinkUrl, int prusaLinkPort, const char* apiKey) {
	init(client, prusaLinkUrl, prusaLinkPort, apiKey);
}

void PrusaLinkApi::init(Client &client, char *prusaLinkUrl, int prusaLinkPort, const char* apiKey) {
  _client         = &client;
  _prusaLinkUrl   = prusaLinkUrl;
  _prusaLinkPort  = prusaLinkPort;
  _usingIpAddress = false;
  _apiKey         = apiKey; // Store the API Key
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
  unsigned long now;

  bool connected;

  if (_usingIpAddress)
    connected = _client->connect(_prusaLinkIp, _prusaLinkPort);
  else
    connected = _client->connect(_prusaLinkUrl, _prusaLinkPort);

  if (connected) {
    if (_debug)
      Serial.println(".... connected to server");

    _client->println(type + " " + command + " HTTP/1.1");
    _client->print("Host: ");
    if (_usingIpAddress)
      _client->println(_prusaLinkIp);
    else
      _client->println(_prusaLinkUrl);
    
    // Send the API Key in the X-Api-Key header
    _client->print("X-Api-Key: ");
    _client->println(_apiKey);

    _client->println("User-Agent: " + String(USER_AGENT));
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
          }
          headers = headers + c;
        } else {
          body = body + c;
        }
        if (c == '\n')
          currentLineIsBlank = true;
        else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    }
  } else {
    if (_debug) {
      Serial.println("connection failed");
    }
  }

  closeClient();

  httpStatusCode = extractHttpCode(statusCode, body);
  if (_debug) {
    Serial.print("\nhttpCode:");
    Serial.println(httpStatusCode);
  }
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
  
  const char* stateStr = root["printer"]["state"] | "UNKNOWN";
  strncpy(printerStats.printerState, stateStr, sizeof(printerStats.printerState) - 1);
  printerStats.printerState[sizeof(printerStats.printerState) - 1] = '\0';

  printerStats.printerStatePrinting = (strcmp(printerStats.printerState, "PRINTING") == 0);
  printerStats.printerStatePaused = (strcmp(printerStats.printerState, "PAUSED") == 0);
  printerStats.printerStateError = (strcmp(printerStats.printerState, "ERROR") == 0) || (strcmp(printerStats.printerState, "ATTENTION") == 0);
  printerStats.printerStateFinished = (strcmp(printerStats.printerState, "FINISHED") == 0);
  printerStats.printerStateReady = (strcmp(printerStats.printerState, "IDLE") == 0);
  printerStats.printerStateBusy = (strcmp(printerStats.printerState, "BUSY") == 0);

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
  
  const char* filenameStr = root["file"]["display_name"] | "No file";
  strncpy(jobInfo.jobFileName, filenameStr, sizeof(jobInfo.jobFileName) - 1);
  jobInfo.jobFileName[sizeof(jobInfo.jobFileName) - 1] = '\0';

  jobInfo.progressCompletion = root["progress"];
  jobInfo.progressPrintTime = root["time_printing"];
  jobInfo.progressPrintTimeLeft = root["time_remaining"];

  return true;
}

bool PrusaLinkApi::printerCommand(const char* gcodeCommand) {
    char postData[POSTDATA_GCODE_SIZE];
    snprintf(postData, POSTDATA_GCODE_SIZE, "{\"command\": \"%s\"}", gcodeCommand);
    sendPostToPrusaLink("/api/v1/printer/command", postData);
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