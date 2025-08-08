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

// --- Helper function for Base64 encoding ---
// Required for HTTP Basic Authentication
const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789+/";

int base64_encode(char *output, const char *input, int inputLen) {
    int i = 0, j = 0;
    int encLen = 0;
    unsigned char a, b, c;
    while (inputLen > 0) {
        a = input[i++];
        inputLen--;
        b = (inputLen > 0) ? input[i++] : 0;
        inputLen--;
        c = (inputLen > 0) ? input[i++] : 0;
        inputLen--;
        output[j++] = b64_alphabet[a >> 2];
        output[j++] = b64_alphabet[((a & 3) << 4) | (b >> 4)];
        output[j++] = b64_alphabet[((b & 15) << 2) | (c >> 6)];
        output[j++] = b64_alphabet[c & 63];
        encLen += 4;
    }
    while ((j % 4) != 0) {
        output[j++] = '=';
    }
    output[j] = '\0';
    return encLen;
}
// --- End of helper function ---


PrusaLinkApi::PrusaLinkApi(void){
	if (_debug)
		Serial.println("Be sure to Call init to setup and start the PrusaLinkApi instance");
}

PrusaLinkApi::PrusaLinkApi(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, const char* username, const char* password) {
  init(client, prusaLinkIp, prusaLinkPort, username, password);
}

void PrusaLinkApi::init(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, const char* username, const char* password) {
  _client         = &client;
  _prusaLinkIp    = prusaLinkIp;
  _prusaLinkPort  = prusaLinkPort;
  _usingIpAddress = true;

  // Create the "username:password" string
  char credentials[64];
  snprintf(credentials, sizeof(credentials), "%s:%s", username, password);

  // Base64-encode the credentials
  char encodedCredentials[96];
  base64_encode(encodedCredentials, credentials, strlen(credentials));

  // Prepare the complete Authorization header
  snprintf(_authHeader, sizeof(_authHeader), "Basic %s", encodedCredentials);
}

PrusaLinkApi::PrusaLinkApi(Client &client, char *prusaLinkUrl, int prusaLinkPort, const char* username, const char* password) {
	init(client, prusaLinkUrl, prusaLinkPort, username, password);
}

void PrusaLinkApi::init(Client &client, char *prusaLinkUrl, int prusaLinkPort, const char* username, const char* password) {
  _client         = &client;
  _prusaLinkUrl   = prusaLinkUrl;
  _prusaLinkPort  = prusaLinkPort;
  _usingIpAddress = false;
  
  // Identical authorization logic as above
  char credentials[64];
  snprintf(credentials, sizeof(credentials), "%s:%s", username, password);
  char encodedCredentials[96];
  base64_encode(encodedCredentials, credentials, strlen(credentials));
  snprintf(_authHeader, sizeof(_authHeader), "Basic %s", encodedCredentials);
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
    
    // Send the pre-formatted Authorization header
    _client->println("Authorization: " + String(_authHeader));

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