/* ___       _        ____       _       _      _    ____ ___
  / _ \  ___| |_ ___ |  _ \ _ __(_)_ __ | |_   / \  |  _ \_ _|
 | | | |/ __| __/ _ \| |_) | '__| | '_ \| __| / _ \ | |_) | |
 | |_| | (__| || (_) |  __/| |  | | | | | |_ / ___ \|  __/| |
  \___/ \___|\__\___/|_|   |_|  |_|_| |_|\__/_/   \_\_|  |___|
.......By Stephen Ludgate https://www.chunkymedia.co.uk.......
.......Redesigned for Prusa Link by Marius Tetard 
....... 08/2025....

*/

#ifndef PrusaLinkApi_h
#define PrusaLinkApi_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Client.h>

#define PLAPI_TIMEOUT       3000
#define POSTDATA_SIZE       256
#define POSTDATA_GCODE_SIZE 50
#define JSONDOCUMENT_SIZE   2048
#define USER_AGENT          "PrusaLinkAPI/1.1.0 (Arduino)"

// Structs bleiben unverändert...
struct prusaLinkStatistics {
  char printerState[20];
  bool printerStatePrinting;
  bool printerStatePaused;
  bool printerStateError;
  bool printerStateReady;
  bool printerStateBusy;
  bool printerStateFinished;
  float printerBedTempActual;
  float printerBedTempTarget;
  float printerTool0TempActual;
  float printerTool0TempTarget;
};

struct prusaLinkJobInfo {
  char jobFileName[64];
  float progressCompletion;
  long progressPrintTime;
  long progressPrintTimeLeft;
};


class PrusaLinkApi {
 public:
  PrusaLinkApi(void);
  // Konstruktor und init nehmen jetzt Benutzername und Passwort
  PrusaLinkApi(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, const char* username, const char* password);
  PrusaLinkApi(Client &client, char *prusaLinkUrl, int prusaLinkPort, const char* username, const char* password);
  void init(Client &client, char *prusaLinkUrl, int prusaLinkPort, const char* username, const char* password);
  void init(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, const char* username, const char* password);

  // Öffentliche Methoden bleiben gleich
  String sendGetToPrusaLink(String endpoint);
  String sendPostToPrusaLink(String endpoint, const char *postData);
  String sendDeleteToPrusaLink(String endpoint);
  bool getPrinterStatus();
  prusaLinkStatistics printerStats;
  bool getJobInfo();
  prusaLinkJobInfo jobInfo;
  bool printerCommand(const char* gcodeCommand);
  // ... (andere Befehle)

  bool _debug          = false;
  int httpStatusCode   = 0;
  String httpErrorBody = "";

 private:
  Client *_client;
  IPAddress _prusaLinkIp;
  bool _usingIpAddress;
  char *_prusaLinkUrl;
  int _prusaLinkPort;
  
  // Speichert den vorbereiteten "Authorization: Basic ..." Header
  char _authHeader[96]; 

  void closeClient();
  int extractHttpCode(String statusCode, String body);
  String sendRequestToPrusaLink(String type, String command, const char *data);
};

#endif