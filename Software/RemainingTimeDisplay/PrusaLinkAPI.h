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
#define JSONDOCUMENT_SIZE   2048 // Erhöht für potenziell größere Antworten
#define USER_AGENT          "PrusaLinkAPI/1.0.0 (Arduino)"

struct prusaLinkStatistics {
  String printerState;
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
  String jobFileName;
  float progressCompletion;
  long progressPrintTime;
  long progressPrintTimeLeft;
};

class PrusaLinkApi {
 public:
  PrusaLinkApi(void);
  PrusaLinkApi(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, String apiKey);
  PrusaLinkApi(Client &client, char *prusaLinkUrl, int prusaLinkPort, String apiKey);
  void init(Client &client, char *prusaLinkUrl, int prusaLinkPort, String apiKey);
  void init(Client &client, IPAddress prusaLinkIp, int prusaLinkPort, String apiKey);
  String sendGetToPrusaLink(String endpoint);
  String sendPostToPrusaLink(String endpoint, const char *postData);
  String sendDeleteToPrusaLink(String endpoint);


  bool getPrinterStatus();
  prusaLinkStatistics printerStats;

  bool getJobInfo();
  prusaLinkJobInfo jobInfo;

  bool _debug          = false;
  int httpStatusCode   = 0;
  String httpErrorBody = "";


  // Drucker-Steuerungsbefehle
  bool printerHome(bool x, bool y, bool z);
  bool printerJog(float x, float y, float z, float speed);
  bool printerExtrude(float amount, float speed);
  bool setBedTemperature(uint16_t t);
  bool setToolTemperature(uint16_t t, int toolId = 0);
  bool printerCommand(const char* gcodeCommand);


  // Job-Befehle
  bool jobStart();
  bool jobPause();
  bool jobResume();
  bool jobStop();


 private:
  Client *_client;
  String _apiKey;
  IPAddress _prusaLinkIp;
  bool _usingIpAddress;
  char *_prusaLinkUrl;
  int _prusaLinkPort;
  const int maxMessageLength = 2000;
  void closeClient();
  int extractHttpCode(String statusCode, String body);
  String sendRequestToPrusaLink(String type, String command, const char *data);
};

#endif