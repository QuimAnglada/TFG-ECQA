/***************************************************************************
 * 
 * TREBALL DE FI DE GRAU 
 * ENGINYERIA QUÍMICA - IQS 
 * CURS 2021/2022
 * 
 * ESTACIÓ DE CONTROL DE QUALITAT D'AIGUA
 * 
 * @INFORMACIÓ GENERAL
 * @Autor: Joaquim Anglada Martínez
 * @Director: Javier Fernández
 * @Codirector: Rubén Mercadé
 * @Data de creació: diumenge 13 de març de 2022
 * @Data última modificació: diumenge 10 de juliol de 2022
 * 
 **************************************************************************/

//------------------LLIBRERIES----------------------------------------------
/***************************************************************************
 * 
 * @Resum: Conjunt de llibreries necessaries pel funcionament de l'estació
 * @Comentari:  Els autors de les diferents llibreries i les versions 
 *              utilitzades es poden trobar en l'apartat Biblioteques del
 *              del fitxer README.md https://github.com/QuimAnglada/TFG-ECQA 
 * 
 *              TFT_          GPIO corresponent.
 *              FS_NO_GLOBALS S'ha de definir prèviament a FS.h i permet la 
 *                            coexistència de LittleFS i la memòria SD.
*               SDMMC_func.h  Fitxer auxiliar amb les funcions necessàries 
*                             pel tractament d'arxius.
*               SoftwareWire myWire(13, 16) 
*                             13 i 16 GPIO SDA i SCL respectivament.
*               PNG_func.h    Fitxer auxiliar amb les funcions necessàries               
*                             pel tractament d'imatges format .png.
*                             
 **************************************************************************/

//-------------- MOD-LCD2.8RTP Screen --------------------------------------
#include "Wire.h"
#include "Adafruit_STMPE610.h"
#include <Arduino_GFX_Library.h>

#define TFT_DC 15
#define TFT_CS 17 
#define TFT_MOSI 2
#define TFT_CLK 14
#define TFT_MISO 0
#define TFT_RST 0

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_CLK, TFT_MOSI, TFT_MISO);
Arduino_GFX *gfx = new Arduino_ILI9341(bus, TFT_RST, 0 );

//--------------------- Littlefs i SDmmc -----------------------------------
#define FS_NO_GLOBALS 
#include "FS.h"
#include "LITTLEFS.h"
#include "SD_MMC.h"
#define SPIFFS LITTLEFS
#include "SDMMC_func.h"   

//-------------------------Wifi, NTP i RTC-------------------------
#include <WiFi.h>
#include "time.h"
#include <ESP32Time.h>

//--------------------------MQTT--------------------
#include <PubSubClient.h>

//------------ ADS1115---------------------
#include "ADS1115.h"
 
#ifdef SOFTWAREWIRE
    #include <SoftwareWire.h>
    SoftwareWire myWire(13, 16); 
    ADS1115<SoftwareWire> ads(myWire);
#else
    #include <Wire.h>
    ADS1115<TwoWire> ads(Wire);
#endif

//------------------------- Sensor temperatura -------------------
#include <OneWire.h>
#include <DallasTemperature.h>

//------------------------- Sensor Sòlids dissolts -------------------
#include <Preferences.h>

//----------------------- PNG images --------------------------------
#include <PNGdec.h>

//---------------------- Imatges PNG --------------------------------
PNG png;
fs::File pngFile;
int16_t w, h, xOffset, yOffset;
#include "PNG_func.h" 

//-------------------------CONFIGURACIONS-----------------------
/***************************************************************************
 * 
 * @Resum: Configuració dels elements que constitueixen l'estació
 * @Comentari:  ssid      Nom de la xarxa Wi-Fi.          
 *              password  Contrasenya de la xarxa Wi-Fi. Substitueix "usuari" 
 *                        i "contrasenya" per les credencials de la teva xarxa.
 *              ntpServer Direcció del servidor Network Time Protocol (NTP)
 *                        que farem servir. 
 *              gmtOffset_sec 
 *                        Variable que s'utilitza per determinar la franja
 *                        horària. Barcelona es troba a la franja horària 
 *                        UTC+1.00 que correspon a un valor de 3600.         
 *              daylightOffset_sec 
 *                        Variable utilitzada per definir si es produeix              
 *                        el canvi d'hora o no dues vegades a l'any.
 *                        Si es modifica dues vegades, el valor és 3600.
 *                        Si l'horari no és modificat, el valor és 0.
 *              mqtt_server 
 *                        La teva adreça IP en la Wi-Fi a la qual l'estació                   
 *                        està connectada. Substitueix "192.168.1.1" per la teva 
 *                        adreça IP.
 *              oneWireBus  
 *                        GPIO on es troba connectat el sensor DS18B20.            
 *              TdsFactor Variable per calcular els sòlids dissolts o la conducti-
 *                        vitat.  Valor 0,5 per calcular els sòlids dissolts.           
 *                                Valor 1 per calcular la conductivitat.
 *              OffSet    Variable per corregir el voltat de sortida. Pren el valor                     
 *                        mínim adquirit pel sensor quan no és connectat en una 
 *                        canonada.
 *                        Valor experimental obtingut: 0,458.
 *             pHSensorPin  
 *                        GPIO sensor de pH.                  
 *             TS_MIN/MAX Valors per calibrar la pantalla tàctil. 
 *             
 **************************************************************************/

//-------------------------- Wifi --------------------
const char* ssid = "usuari";
const char* password = "contasenya";

//------------------------- NTP -----------------------
const char* ntpServer = "europe.pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

//---------------------- Servidor mqtt ---------------
int pot;
char data_pot[4]="";
char data_temp[12]="";
char data_tds[12]="";
char data_tss[12]="";
char data_pre[12]="";
char data_ph[12]="";
char data_digital[2]="";
String messageData;

const char* mqtt_server="192.168.1.1"; 

WiFiClient esp32;
PubSubClient client(esp32);

long lastMsg = 0;
long tdhtx = 0;
char msg[50];
int value = 0;

//----------------------- RTC --------------------------
ESP32Time rtc;

//------------ Funció LITTLEFS to SD MMC ---------------
char *pathChar;

//------------ ADS1115 ---------------------
int16_t adc0,adc1,adc2,adc3;

//------------ Sensor temperatura ---------------------
const int oneWireBus = 18;     
OneWire oneWire(oneWireBus); 
DallasTemperature sensorT(&oneWire);
float temperatureC, lastTemperature = 0, maxT, minT = 1000;

//------------ Sensor sòlids totals dissolts -------------------
Preferences preferences;
int tdsValue, lastTdsValue, TDS, maxTds, minTds = 10000;
float TdsFactor = 0.5;
float voltage, kValue;
long ecValue, ecValue25;
String DataTDS, HoraTDS;

//------------ Sensor Terbolesa-------------------
float voltageTSS, NTU, lastNTU = -1, maxNTU = -1, minNTU = 10000;

//------------  Sensor Pressió -------------------
const float  OffSet = 0.458 ;
float V, P, lastPValue, maxP, minP = 1000;

//------------ Sensor pH -------------------
float aValue, nValue, pendent, ordenada, pH, lastpHValue, maxpH, minpH = 20;
String DatapH4, HorapH4, DatapH7, HorapH7;
int pHSensorPin = 39;

//---------------------- Pantalla tàctil --------------------
#define TS_MINX 290
#define TS_MINY 285
#define TS_MAXX 7520
#define TS_MAXY 7510
#define TS_I2C_ADDRESS 0x4d 
Adafruit_STMPE610 ts = Adafruit_STMPE610();

//-------------------- Variables Globals -----------------------------
/***************************************************************************
 * 
 * @Resum: Definició de variables auxiliars.
 * @Comentari:  modeOpe variable on s'emmagatzema el mode d'operació escollit.
 *              i       Nombre de mesures que s'han realitzat.
 *              a       Nombre de vegades que s'han emmagatzemat les dades de 
 *                      littlefs a la targeta SD.
 *              TimeNow Hora en la qual s'ha tocat la pantalla expressada en segons.        
 *              TimePressed 
 *                      Hora en la qual s'havia tocat la pantalla per última 
 *                      vegada expressada en segons.
 *              TimeBackup  
 *                      Hora actual expressada en segons.
 *              TimeLastBackup 
 *                      Hora en la qual s'ha realitzat l'última còpia de
 *                      de seguretat expressada en segons.
 *              Minuts  Interval de temps entre còpies de seguretat.         
 *             
 **************************************************************************/

int modeOpe, i = 0; 
int ButtonPressed = 0;
int a = 0; 
unsigned long TimeNow = 0; 
unsigned long TimePressed = 0;
unsigned long TimeBackup = 0;
unsigned long TimeLastBackup = 0;
int Minuts = 30; 

//-------------------- SETUP -----------------------------
/***************************************************************************
 * 
 * @Resum: Inicialització de l'estació i selecció del mode d'operació.
 * @Comentari:  Si el mode d'operació escollit és el LOCAL (modeOpe = 1)
 *              es desconnecta l'estació de la xarxa Wi-Fi perquè aquesta no 
 *              es requerida pel funcionament.
 *              Si el mode d'operació escollit és el MQTT (modeOpe = 2) es 
 *              connecta l'estació al broker (client.setServer(mqtt_server,
 *              1883)) on s'indica l'adreça IP amb mqtt_server i el port
 *              corresponent 1883.
 * 
 **************************************************************************/

void setup() {
 Serial.begin(115200);
 delay(500);
 
 //----------------------Inicialitzacions-------------------------
  initSCREEN();
  initWiFi();
  initNTP_RTC ();
  sensorT.begin(); 
  initTOUCH ();
  initSDMMC ();
  initLITTLEFS ();
  newDataFile();
  initADS ();
  initPreferences();
  initPreferences2();
  initPreferences3();
  displayIQSlogo();
  loadSCREEN();
  modeSCREEN();

  while (ButtonPressed != 9 &&  ButtonPressed != 10) {
            ButtonPressed = Get_Button();
        }
  if (ButtonPressed == 9) {
      modeOpe = 1;
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
  } 
  else if (ButtonPressed == 10) {
      modeOpe = 2;
      client.setServer(mqtt_server,1883);
      client.setCallback(callback);
  } 
        
  backgroundSCREEN();
  mainSCREEN();

  Serial.println("");
  Serial.println("---------------Mesura Paràmetres---------------");
  TimeLastBackup = rtc.getSecond() + rtc.getMinute() * 60 + rtc.getHour() * 3600;
}

//-------------------- LOOP -----------------------------
/***************************************************************************
 * 
 * @Resum:  Seguit d'instruccions que s'executen en bucle pel funcionament de
 *          l'estació.
 * @Comentari:  En primer lloc, es reconnecta l'estació al broker en cas que 
 *              aquesta hagi estat desconnectada a través de la funció reconnect().
 *              Aquesta acció només té lloc si el mode d'operació escollit és el 
 *              MQTT (modeOpe = 2).
 *              A continuació, es mesuren els paràmetres i registren les dades 
 *              adquirides amb la funció writeLITTLEFS().
 *              Amb la funció Get_Button() s'ofereix a l'usuari la possibilitat 
 *              d'interactuar amb l'estació a través de la pantalla tàctil.
 *              Depenen de quin botó s'hagi clicat s'executa una funció o altra, 
 *              s'exposa amb més detall cada una de les funcions al final del bucle
 *              loop.
 *              Si fa més de 30 minuts des de l'última còpia de seguretat es realitza 
 *              una de nova amb la funció LITTLEFStoSD().
 *              En últim lloc, si el mode d'operació escollit és el MQTT (modeOpe = 2)
 *              es publiquen les mesures realitzades. Les dades es publiquen en format 
 *              char. Per aquest motiu els números en els diferents formats (int o float)
 *              són convertits a char mitjançant la funció sprintf.
 *                p. ex: sprintf (data_temp,"%3.2f",temperatureC);
 *              Per publicar una mesura s'indica el tòpic i el char a la funció 
 *              client.publsih(), el tòpic és el mateix que s'ha d'introduir al software 
 *              Node-Red per rebre les publicacions.
 *                p. ex: client.publish("temperatura", data_temp);                    
 *        
 **************************************************************************/
 
void loop() {
  if (modeOpe == 2) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }
  
  writeLITTLEFS ();
    
  ButtonPressed = Get_Button();
    if (ButtonPressed == 2) {
        secondSCREEN();
         while (ButtonPressed != 1) {
            ButtonPressed = Get_Button();
        }
        if (ButtonPressed == 1) {
            mainSCREEN();
            ButtonPressed = 0;
        }
      }
      
      if (ButtonPressed == 3) {
        displayInfo();
        while (ButtonPressed != 1) {
            ButtonPressed = Get_Button();
        }
        if (ButtonPressed == 1) {
            mainSCREEN();
            ButtonPressed = 0;
        }  
      }
    
      if (ButtonPressed == 4) {
        displaySD();
        LITTLEFStoSD ();
        delay(3000);
        gfx->begin();
        mainSCREEN();
        }  

      if (ButtonPressed == 5) {
        displayTDS();
        delay(2000);
        mainSCREEN();
      }  

      if (ButtonPressed == 6) {
        displayCAL();
        while (ButtonPressed != 1 &&  ButtonPressed != 2) {
            ButtonPressed = Get_Button();
        }
        if (ButtonPressed == 1) {
            mainSCREEN();
            ButtonPressed = 0;
        }  
        if (ButtonPressed == 2) {
          displayCALpH4();
          ButtonPressed = 0;
          while (ButtonPressed != 1 &&  ButtonPressed != 2) {
            ButtonPressed = Get_Button();
          }
          if (ButtonPressed == 1) {
            mainSCREEN();
            ButtonPressed = 0;
          }  
          if (ButtonPressed == 2) {
            displayCALpH7();
            while (ButtonPressed != 1) {
              ButtonPressed = Get_Button();
            }
            if (ButtonPressed == 1) {
              mainSCREEN();
              ButtonPressed = 0;
            }  
          }
        } 
      } 

    if (ButtonPressed == 7) {
        displaypH4();
        delay(2000);
        mainSCREEN();
      }  
      
    if (ButtonPressed == 8) {
        displaypH7();
        delay(2000);
        mainSCREEN();
      }   
        

    TimeBackup = rtc.getSecond() + rtc.getMinute() * 60 + rtc.getHour() * 3600;
    if ( abs( TimeBackup - TimeLastBackup) >= (Minuts * 60)) {
      TimeLastBackup = TimeBackup;
      displaySD();
      LITTLEFStoSD ();
      delay(6000);
      gfx->begin();
      mainSCREEN();
    }        
    
  if (modeOpe == 2) {
    sprintf (data_temp,"%3.2f",temperatureC); 
    client.publish("temperatura", data_temp); 
  
    sprintf (data_tds,"%d",TDS); 
    client.publish("tds", data_tds); 
  
    sprintf (data_tss,"%3.0f",NTU); 
    client.publish("tss", data_tss); 
    
    sprintf (data_pre,"%3.2f",P); 
    client.publish("pressio", data_pre); 

    sprintf (data_ph,"%3.2f",pH); 
    client.publish("pH", data_ph); 
  }
  delay(3000);
}

//---------------------------FUNCIONS----------------------
/***************************************************************************
 * @Funció: initSCREEN()
 * @Resum:  Inicialització de la pantalla tàctil.  
 * @Comentari:  S'utilitza la funció PrintCharTFT d'ara endavant en lloc del 
 *              conjunt de funcions exposades a continuació si la mida de la
 *              lletra és 1. Aquesta mida de lletra no es veia correctament 
 *              en la pantalla tàctil.
 *                p. ex:  gfx->setTextColor(WHITE);
 *                        gfx->setCursor(156, 120); 
 *                        gfx->setTextSize(2);
 *                        gfx->println(temperatureC);                              
 *        
 **************************************************************************/
void initSCREEN() {
  gfx->begin();
  gfx->fillScreen(BLACK);
  PrintCharTFT("Connectant l'Estacio a la wifi...", 21, 75, WHITE, BLACK, 1);
}

/***************************************************************************
 * @Funció: initWiFi()
 * @Resum:  Connexió a la xarxa Wi-Fi.
 * @Comentari:  WiFi.mode(WIFI_STA) Es configura la Wi-Fi estació, dispositiu                                   
 *                                  des del qual et pots connectar a una xarxa.
 *                                  Es pot produir el cas contrari i definir 
 *                                  l'estació com l'acces point. Aparell on els 
 *                                  altres dispositius es connecten. 
 *                                    p. ex: WIFI_AP_STA
 *                                  En cas que l'estació no es trobi connectada    
 *                                  a la Wi-Fi, aquesta roman en espera mostrant 
 *                                  "." cada 1 s en el monitor serial fins que 
 *                                  esdevingui la seva connexió.
 *                                           
 **************************************************************************/
 
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("--------------Configuració WIFI--------------");
  Serial.printf("Connectant-se a la xarxa wifi: ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(" Connectat");
  Serial.print("IP Local: ");
  Serial.println(WiFi.localIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
  PrintCharTFT("Fet", 21, 85, WHITE, BLACK, 1);
  delay(1000);
}

/***************************************************************************
 * @Funció: initNTP_RTC ()
 * @Resum:  Configuració i obtenció de la data i l'hora des d'internet.
 *                                           
 **************************************************************************/
 
void initNTP_RTC () {
  Serial.println("");
  Serial.println("-------------Configuració NTP-RTC------------");
  PrintCharTFT("Config. Network time protocol...", 21, 100, WHITE, BLACK, 1);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); 
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)){  
    rtc.setTimeStruct(timeinfo); 
    Serial.println("Fet");
    PrintCharTFT("Fet", 21, 110, WHITE, BLACK, 1);
  }
  else {
    Serial.println("Error");
    PrintCharTFT("Error", 21, 110, WHITE, BLACK, 1);
  }
  delay(1000);
}

/***************************************************************************
 * @Funció: initSDMMC()
 * @Resum:  Inicialització de la targeta micro SD. S'indica si aquesta ha presentat 
 *          algún inconvenient com per exemple que hagi estat mal connectada.
 *          També s'indica el tipus i memòria de targeta. 
 * @Comentari:  Es recomana emprar targetes micro SD amb un emmagatzematge 
 *              igual o inferior a 32 GB. Si la targeta utilitzada és superior 
 *              s'ha de formatar per poder ser utilitzada. 
 *                                           
 **************************************************************************/
 
void initSDMMC () {
  Serial.println("");
  Serial.println("--------------Configuració SD MMC--------------");
  PrintCharTFT("Config. targeta SD MMC...", 21, 125, WHITE, BLACK, 1);
  if (!SD_MMC.begin()) {
    Serial.println("La muntura de la targeta ha fallat");
    gfx->begin();
    PrintCharTFT("Error -> Reinicia l'equip", 21, 135, WHITE, BLACK, 1);
    return;
  }
  else {
    Serial.println("La muntura de la targeta s'ha realitzat correctament");
    gfx->begin();
    PrintCharTFT("Fet", 21, 135, WHITE, BLACK, 1); 
  }
  
  uint8_t cardType = SD_MMC.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No hi ha cap targeta SD connectada");
    PrintCharTFT("Error -> Targeta no connectada", 21, 135, WHITE, BLACK, 1);
    return;
  }

  Serial.print("Tipus de targeta SD: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("Desconegut");
  }

  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("Capacitat d'emmagatzematge de la targeta SD: %lluMB\n", cardSize);
  SD_MMC.end();
  delay(1000);
}

/***************************************************************************
 * @Funció: initLITTLEFS()
 * @Resum:  Inicialització del sistema de fitxers. Es comprova que s'hagi 
 *          inialitzat correctament, l'espai total, l'espai lliure i l'espai 
 *          utilitzat. 
 *                                           
 **************************************************************************/

void initLITTLEFS () {
  Serial.println("");
  Serial.println("------------Configuració Little FS------------");
  Serial.print(F("Inizialitzant FS..."));
  PrintCharTFT("Config. Little FS...", 21, 150, WHITE, BLACK, 1);
  
  if (LITTLEFS.begin()){
    Serial.println(F("Fet"));
    gfx->begin();
    PrintCharTFT("Fet", 21, 160, WHITE, BLACK, 1);
  }else{
    Serial.println(F("Error"));
    gfx->begin();
    PrintCharTFT("Error", 21, 160, WHITE, BLACK, 1);
  }
  
  unsigned int totalBytes = LITTLEFS.totalBytes();
  unsigned int usedBytes = LITTLEFS.usedBytes();
  unsigned int freeBytes  = totalBytes - usedBytes;
    
  Serial.println("Informació del sistema de fitxers:");
  
  Serial.print("Espai total:      ");
  Serial.print(totalBytes);
  Serial.println("byte");

  Serial.print("Espai total utilitzat: ");
  Serial.print(usedBytes);
  Serial.println("byte");

  Serial.print("Espai total lliure: ");
  Serial.print(freeBytes);
  Serial.println("byte");

  delay(1000);
}

/***************************************************************************
 * @Funció: initTOUCH()
 * @Resum:  Inicialització de la part tàctil de la pantalla.  
 * @Comentari:  La pantalla es comunica amb l'ESP32-EVB-EA-IND amb el protocol
 *              de comunicació SPI i el component tàctil ho fa a través de l'I2C.
 *                                           
 **************************************************************************/
void initTOUCH() {
  Wire.begin();
  pinMode(TFT_DC, OUTPUT);
  delay(1000);
  ts.begin(TS_I2C_ADDRESS);  
}

/***************************************************************************
 * @Funció: initPreferences/2/3()
 * @Resum:  Inicialització de la memòria flash per guardar dades permanentment.  
 *          Es disposa de tres inicialitzacions cada una corresponent a una 
 *          variable i la data i l'hora en la qual es van adquirir.
 * @Comentari: 
 *      preferences.begin("kValue", false)  
 *              Es defineix un espai d'emmagatzamatge amb el nom indicat (kValue)  
 *              en la memòria flash. L'argument false índica que es pot llegir i 
 *              escriu mentre que l'argument true només permet la lectura de la                           
 *              memòria.                             
 *      kValue = preferences.getFloat("kValue", 0.000) 
 *              Per llegir el valor s'utilitza la funció get.Float. Si no troba                                    
 *              cap valor, retorna 0.000. 
 *                                           
 **************************************************************************/
 
void initPreferences(){
  Serial.println("");
  Serial.println("-----------Configuració Preferences-----------");
  preferences.begin("kValue", false); 
  kValue = preferences.getFloat("kValue", 0.000);
  Serial.printf("kValue actual: %.3f\n", kValue);
  
  preferences.begin("DataTDS", false);
  String DataTDS = preferences.getString("DataTDS", "00/00/0000");
  Serial.print("Data última calibració TDS: ");
  Serial.println(DataTDS);
  
  preferences.begin("HoraTDS", false);
  String HoraTDS = preferences.getString("HoraTDS", "00:00:00");
  Serial.print("Hora última calibració TDS: ");
  Serial.println(HoraTDS);

  preferences.end();
}

void initPreferences2(){
  preferences.begin("aValue", false); 
  aValue = preferences.getFloat("aValue", 0.000);
  Serial.printf("aValue actual: %.3f\n", aValue);

  preferences.begin("DatapH4", false);
  String DatapH4 = preferences.getString("DatapH4", "00/00/0000");
  Serial.print("Data última calibració pH 4,0: ");
  Serial.println(DatapH4);
  
  preferences.begin("HorapH4", false);
  String HorapH4 = preferences.getString("HorapH4", "00:00:00");
  Serial.print("Hora última calibració pH 4,0: ");
  Serial.println(HorapH4);

  preferences.end();
}

void initPreferences3(){
  preferences.begin("nValue", false); 
  nValue = preferences.getFloat("nValue", 0.000);
  Serial.printf("nValue actual: %.3f\n", nValue);

  preferences.begin("DatapH7", false);
  String DatapH7 = preferences.getString("DatapH7", "00/00/0000");
  Serial.print("Data última calibració pH 7,0: ");
  Serial.println(DatapH7);
  
  preferences.begin("HorapH7", false);
  String HorapH7 = preferences.getString("HorapH7", "00:00:00");
  Serial.print("Hora última calibració pH 7,0: ");
  Serial.println(HorapH7);
  
  preferences.end();
}

/***************************************************************************
 * @Funció: displayIQSlogo(
 * @Resum:  Es mostra el logotip corporatiu de l'Institut Químic de Sarrià
 *          (IQS). Entitat on s'ha realitzat aquest treball de fi de grau.     
 *                                           
 **************************************************************************/

void displayIQSlogo(){
  Serial.println("");
  Serial.println("----------Mostrar el logotip de l'IQS----------");
  gfx->begin();
  gfx->fillScreen(BLACK);
  DrawPNG("/IQS100_65.png", 70, 125);
  delay(2000);
}

/***************************************************************************
 * @Funció: newDataFile()
 * @Resum:  Eliminació de l'antic fitxer on es registraven les dades prèviament 
 *          a ser traspassades a la memòria SD amb la funció LITTLEFS.remove("
 *          /Dades.csv"). 
 *          Seguidament, es crea el nou fitxer amb la funció LITTLEFS.open("
 *          /Dades.csv", FILE_APPEND). On s'insereixen els encapçalaments de la 
 *          taula on seran recollides les dades. 
 * @Comentari: 
 *       LITTLEFS.open("/Dades.csv", FILE_APPEND)
 *          Dades correspon al nom i .csv al format de l'arxiu. El segon argument, 
 *          FILE_APPEND permet afegir dades a l'arxiu sense sobreescriure el 
 *          contingut previ. 
 *                                           
 **************************************************************************/

void newDataFile() {
  if (LITTLEFS.remove("/Dades.csv")) {
    Serial.println("");
    Serial.println("---------Configuració arxiu Dades.csv---------");
    Serial.println("S'ha suprimit el fitxer antic Dades.csv");
    gfx->begin();
    PrintCharTFT("Eliminant versio anterior Dades.csv", 21, 175, WHITE, BLACK, 1);
    PrintCharTFT("Fet", 21, 185, WHITE, BLACK, 1);
    PrintCharTFT("Creant arxiu Dades.csv actual...", 21, 200, WHITE, BLACK, 1);
    delay(1000);
  }

  fs::File dataFile = LITTLEFS.open("/Dades.csv", FILE_APPEND);  
  if (dataFile){
    Serial.println("El fitxer de dades nou s'ha creat correctament");
    dataFile.print("Data"); dataFile.print(';');
    dataFile.print("Hora"); dataFile.print(';');
    dataFile.print("Temperatura [ºC]"); dataFile.print(';');
    dataFile.print("TDS [ppm]"); dataFile.print(';');
    dataFile.print("Terb [NTU]"); dataFile.print(';');
    dataFile.print("Pressio [kPa]"); dataFile.print(';');
    dataFile.print("pH"); dataFile.println(';');
    dataFile.close();
    gfx->begin();
    PrintCharTFT("Fet", 21, 210, WHITE, BLACK, 1);
  }else{
    Serial.println("Problema en crear o escriure un fitxer!");
    gfx->begin();
    PrintCharTFT("Error -> Reinicia l'equip", 21, 210, WHITE, BLACK, 1);
  }
  delay(1000);
}

/***************************************************************************
 * @Funció: initADS()
 * @Resum:  Inicialització i configuració del convertidor de senyal analògic  
 *          a digital.
 * @Comentari: 
 *          ads.begin(0x48) Comprovació que s'ha realitzat correctament la
 *                          inicialització.
 *          L'ADS1115 presenta dos modes d'operació: el mode d'un sol extrem                
 *          o el mode diferencial. En el mode d'un sol extrem l'ADC llegeix
 *          la diferència entre les quatre entrades analògiques (A0-A3) de 
 *          l'ADS1115 i el GND. En el mode diferencial permet llegir la 
 *          diferència entre les entrades A0 i A1, per una banda, i A2 i A3 per 
 *          l'altra. 
 *          
 *          Es programa el mode d'operació d'un sol extrem a través de les 
 *          següents línies de codi:
 *            ads.setOperateMode(ADS1115_OS_SINGLE);
 *            ads.setOperateStaus(ADS1115_MODE_SINGLE);
 *           
 *          El guany ("ganancia") del convertidor pot ser modificada. S'utilitza  
 *          el guany que ve establert per defecte. 
 *            ads.setPGAGain(ADS1115_PGA_6_144);                
 *                                           
 **************************************************************************/

void initADS() {
  Serial.println("");
  Serial.println("-------------Configuració ADS1115------------");
  PrintCharTFT("Config. ADS1115", 21, 225, WHITE, BLACK, 1);
  while(!ads.begin(0x48)){
      Serial.println("Ha fallat la inicialització");
      PrintCharTFT("Error", 21, 235, WHITE, BLACK, 1);
      delay(1000);
  }
  Serial.println("Fet");
  PrintCharTFT("Fet", 21, 235, WHITE, BLACK, 1);
  delay(5000);
  ads.setOperateMode(ADS1115_OS_SINGLE);
  ads.setOperateStaus(ADS1115_MODE_SINGLE);
  ads.setPGAGain(ADS1115_PGA_6_144);    
  }

/***************************************************************************
 * @Funció: writeLITTLEFS ()
 * @Resum:  Mesura dels diferents paràmetres i còpia al fitxer Dades.csv.
 *          En primer lloc, s'obre el fitxer amb la funció LITTLEFS.open(),
 *          tot seguit, es prenen les mesures i són registrades a Dades.csv.
 *          En últim lloc, mitjançant una seria de condicionals les últimes 
 *          dades adquirides són mostrades en la pantalla tàctil si són di-
 *          ferents a les prèvies. Simultàniament, es guarden els màxims i 
 *          mínims de cada paràmetre també a través de condicionals. Aquests
 *          valors són mostrats a l'usuari si aquest clica el botó "->".
 * @Comentari: 
 *          sensorT.getTempCByIndex(0) 
 *              S'obté la temperatura en graus Celsius.                 
 *          temperatureC = 1.01911*temperatureC - 1.01037
 *              Recta de calibrat del termòmetre obtinguda experimentalment.
 *          TDS = getTDS ()/ NTU = getTSS()/ pH = getpH()                                
 *              Aquestes tres funcions es comenten amb més detall en les properes
 *              lines de codi.
 *          ads.getConversionResults(channel0)    
 *              Lectura del voltatge en l'entrada A0 de l'ADS1115 on es troba 
 *              connectat el sensor de pressió. La pressió es calcula amb les
 *              següents equacions.
 *                V = (adc0*0.1875)/1000    
 *                P = (V - OffSet) * 250
 *              Les equacions han estat extretes de la pàgina web del fabricant:
 *              https://wiki.dfrobot.com/Gravity__Water_Pressure_Sensor_SKU__SEN0257
 *              
 **************************************************************************/

void writeLITTLEFS () {
  fs::File dataFile = LITTLEFS.open("/Dades.csv", FILE_APPEND);

  //Sensor temperatura
  sensorT.requestTemperatures(); 
  temperatureC = sensorT.getTempCByIndex(0);
  temperatureC = 1.01911*temperatureC - 1.01037 ;

  //Sensor sòlids dissolts
  TDS = getTDS ();

  //Sensor terbolesa
  NTU = getTSS();
  
  //Sensor pressió
  adc0 = ads.getConversionResults(channel0);
  V = (adc0*0.1875)/1000;   
  P = (V - OffSet) * 250;   

  //Sensor pH
  pH = getpH();
        
  if (dataFile){
    //TEMPERATRA
    //Serial.println("");
    //Serial.println("Writing temperature in flas data file!");
    dataFile.print(rtc.getTime("%D")); dataFile.print(';');
    dataFile.print(rtc.getTime("%H:%M:%S")); dataFile.print(';');
    dataFile.print(temperatureC); dataFile.print(';');  
    dataFile.print(TDS); dataFile.print(';'); 
    dataFile.print(NTU); dataFile.print(';'); 
    dataFile.print(P); dataFile.print(';');  
    dataFile.print(pH); dataFile.println(';'); 
    dataFile.close();

    gfx->fillRect(0, 0, 48, 8, BLACK);
    PrintCharTFT(rtc.getTime("%d/%m/%y  "), 0, 0, WHITE, BLACK, 1);
    gfx->fillRect(60, 0, 48, 8, BLACK);
    PrintCharTFT(rtc.getTime("%H:%M:%S  "), 60, 0, WHITE, BLACK, 1);

    Serial.println("");
    Serial.print(i); Serial.println(": ");
    Serial.print("Temperatura: ");
    Serial.print(temperatureC);
    Serial.println(" ºC");
    Serial.print("TDS: ");
    Serial.print(TDS);
    Serial.println(" ppm");
    Serial.print("Terbolesa: ");
    Serial.print(NTU,0);
    Serial.println(" NTU");
    Serial.print("Pressió: ");
    Serial.print(P, 2);
    Serial.println(" KPa");
    Serial.print("pH: ");
    Serial.println(pH, 2);
    
    gfx->setTextColor(WHITE);  
    gfx->setCursor(156, 120); gfx->setTextSize(2);
    if (temperatureC != lastTemperature){
      gfx->fillRect(156, 120, 84, 16, BLACK);
      gfx->println(temperatureC);
      lastTemperature = temperatureC;
    }

    gfx->setCursor(156, 155); gfx->setTextSize(2);
    if (tdsValue != lastTdsValue){
      gfx->fillRect(156, 155, 84, 16, BLACK);
      gfx->println(tdsValue,0);
      lastTdsValue = tdsValue;
    }

    gfx->setCursor(156, 190); gfx->setTextSize(2);
    if (NTU != lastNTU){
      gfx->fillRect(156, 190, 84, 16, BLACK);
      gfx->println(NTU, 0);
      lastNTU = NTU;
    }
    
    gfx->setCursor(156, 225); gfx->setTextSize(2);
    if (P != lastPValue){
      gfx->fillRect(156, 225, 84, 16, BLACK);
      gfx->println(P,2);
      lastPValue = P;
    }

    gfx->setCursor(156, 260); gfx->setTextSize(2);
    if (pH != lastpHValue){
      gfx->fillRect(156, 260, 84, 16, BLACK);
      gfx->println(pH,2);
      lastpHValue = pH;
    }

    if (temperatureC > maxT){
      maxT = temperatureC;
    }
    else if (temperatureC < minT) {
      minT = temperatureC;
    }

    if (tdsValue > maxTds){
      maxTds = tdsValue;
    }
    else if (tdsValue < minTds) {
      minTds = tdsValue;
    }

    if (NTU > maxNTU){
      maxNTU = NTU;
    }
    else if (NTU < minNTU) {
      minNTU = NTU;
    }

    if (P > maxP){
      maxP = P;
    }
    else if (P < minP) {
      minP = P;
    }

    if (pH > maxpH){
      maxpH = pH;
    }
    else if (pH < minpH) {
      minpH = pH;
    }
    
  }else{
    Serial.println("Problema en crear o escriure un fitxer!");
  }
  i++;
}

/***************************************************************************
 * @Funció: LITTLEFStoSD ()
 * @Resum:  Funció per passar les dades des del sistema de fitxers (Littlefs)
 *          a la targeta de memòria micro SD. Es comprova que s'hagi fet la 
 *          muntura de la targeta de memòria correctament. S'anomena l'arxiu 
 *          en què es registraran les dades i s'obren el fitxer emissor i el 
 *          receptor de la informació. L'emissor és LittleFS i el receptor és
 *          SD MMC. És dur a terme el traspàs i es tanquen els diferents 
 *          sistemes que hi han intervingut.        
 * @Comentari: 
 *          SD_MMC.begin()
 *              Comprovació que la muntura s'ha realitzat correctament.
 *          String filename = "/" + rtc.getTime("%Y%m%d_%H%M") + ".csv";
 *          pathChar = const_cast<char*>(filename.c_str());    
 *              Creació del nom de l'arxiu que contindrà les dades. 
 *          LITTLEFS.open()/ SD_MMC.open(pathChar, FILE_WRITE) 
 *              Funcions per obrir l'arxiu emissor i receptor. En el cas del 
 *              receptor, s'ha d'indicar on i amb quin nom es vol guardar 
 *              el fitxer. El segon argument, FILE_WRITE, permet escriure i  
 *              llegir en  el document.
 *          En el bucle while es fa el traspàs d'informació.
 *          
 **************************************************************************/

void LITTLEFStoSD () {
  Serial.println("");
  Serial.println("----------Dades de Little FS a SDMMC----------");
  if (!SD_MMC.begin()) {
    Serial.println("La muntura de la targeta ha fallat");
    return;
  }
  else {
    Serial.println("La muntura de la targeta s'ha realitzat correctament");
  }
 
  String filename = "/" + rtc.getTime("%Y%m%d_%H%M") + ".csv";
  pathChar = const_cast<char*>(filename.c_str());

  fs::File sourceFile = LITTLEFS.open("/Dades.csv");

  fs::File destFile = SD_MMC.open(pathChar, FILE_WRITE);
  if (!destFile) {
    Serial.println("No es pot obrir el fitxer de dades de la memòria SD");
    return;
  }
  static uint8_t buf[512];
  while ( sourceFile.read( buf, 512) ) {
    destFile.write( buf, 512 );
  }
  delay(100);
  destFile.close();
  sourceFile.close();
  SD_MMC.end();
  Serial.println("Aturant SD MMC");
  a++;
}

/***************************************************************************
 * @Funció: loadSCREEN()
 * @Resum:  Pantalla de càrrega on es dóna la benvinguda a l'usuari/ària.         
 *          
 **************************************************************************/

void loadSCREEN() {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->setCursor(36, 60);
  gfx->println("Benvingut/da a");
  gfx->setCursor(0,100);
  gfx->println("l'Estacio de Control");
  gfx->setCursor(6,140);
  gfx->println("de Qualitat d'Aigua");
  gfx->setTextColor(ORANGE);  gfx->setTextSize(6);
  gfx->setCursor(12, 200);
  gfx->println("ECQA-1"); 
  
  delay(5000);
}

/***************************************************************************
 * @Funció: modeSCREEN()
 * @Resum:  Pantalla on és seleccionat el mode d'operació a través de dos 
 *          botons. El mode d'operació potser LOCAL o MQTT.          
 *          En el mode LOCAL només és necessària la Wi-Fi fins a l'obtenció 
 *          de la data i l'hora. 
 *          En el mode MQTT la Wi-Fi és necessària en tot moment perquè les 
 *          dades mesurades són enviades constantment al monitor de control
 *          (Broker+Node-Red). 
 *          
 **************************************************************************/

void modeSCREEN() {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->setCursor(36, 60);
  gfx->println("Seleccioni un");
  gfx->setCursor(12,90);
  gfx->println("un mode d'operacio");
  gfx->fillRoundRect(65, 125, 110, 44, 6, BLUE);
  gfx->drawRoundRect(65, 125, 110, 44, 6, WHITE);
  gfx->setCursor(75,135); gfx->setTextSize(3);
  gfx->println("LOCAL");
  gfx->fillRoundRect(65, 180, 110, 44, 6, BLUE);
  gfx->drawRoundRect(65, 180, 110, 44, 6, WHITE);
  gfx->setCursor(84,190);
  gfx->println("MQTT");
}

/***************************************************************************
 * @Funció: backgroundSCREEN()
 * @Resum:  Pantalla que es manté constant mentre funcioni l'ECQA. Es mostra 
 *          el títol, els botons per habilitar les diferents funcions i un
 *          parell de condicionals per mostrar en quin mode d'operació s'està
 *          treballant.
 *          
 **************************************************************************/

void backgroundSCREEN(){
  gfx->fillScreen(BLACK);

//Títol
  gfx->setTextColor(ORANGE);  gfx->setTextSize(5);
  gfx->setCursor(0,15);
  gfx->print("ECQA-1");

// Botons
  //Information
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->fillRoundRect(190, 0, 50, 50, 6, BLUE);
  gfx->setCursor(210, 20); gfx->setTextSize(2);
  gfx->println("I");
  gfx->drawRoundRect(190, 0, 50, 50, 6, WHITE);

  //Enrere
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->fillRoundRect(0, 280, 40, 40, 6, BLUE);
  gfx->setCursor(8, 292); gfx->setTextSize(2);
  gfx->println("<-");
  gfx->drawRoundRect(0, 280, 40, 40, 6, WHITE);

  //Endavant
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->fillRoundRect(40, 280, 40, 40, 6, BLUE);
  gfx->setCursor(48, 292); gfx->setTextSize(2);
  gfx->println("->");
  gfx->drawRoundRect(40, 280, 40, 40, 6, WHITE);

  //SD
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->fillRoundRect(80, 280, 40, 40, 6, BLUE);
  gfx->setCursor(88, 292); gfx->setTextSize(2);
  gfx->println("SD");
  gfx->drawRoundRect(80, 280, 40, 40, 6, WHITE); 

  //TDS
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->fillRoundRect(120, 280, 40, 40, 6, BLUE);
  gfx->setCursor(122, 292); gfx->setTextSize(2);
  gfx->println("TDS");
  gfx->drawRoundRect(120, 280, 40, 40, 6, WHITE); 

  //pH4
    gfx->setTextColor(WHITE);  gfx->setTextSize(2);
    gfx->fillRoundRect(160, 280, 40, 40, 6, BLUE);
    gfx->setCursor(165, 292); gfx->setTextSize(2);
    gfx->println("pH");
    PrintCharTFT("4", 189, 296, WHITE, BLUE, 1);
    gfx->drawRoundRect(160, 280, 40, 40, 6, WHITE); 
  
  //pH7
    gfx->setTextColor(WHITE);  gfx->setTextSize(2);
    gfx->fillRoundRect(200, 280, 40, 40, 6, BLUE);
    gfx->setCursor(205, 292); gfx->setTextSize(2);
    gfx->println("pH");
    PrintCharTFT("7", 229, 296, WHITE, BLUE, 1);
    gfx->drawRoundRect(200, 280, 40, 40, 6, WHITE); 

//Mode
  if (modeOpe == 1){
    PrintCharTFT("LOCAL", 130, 0, WHITE, BLACK, 1);
  }
  else if (modeOpe == 2) {
    PrintCharTFT("MQTT", 130, 0, WHITE, BLACK, 1);
  }
}

/***************************************************************************
 * @Funció: mainSCREEN()
 * @Resum:  Pantalla que mostra el subtítol i indica quin cada dada a quin
 *          paràmetre pertany i les seves unitats. 
 *          
 **************************************************************************/

void mainSCREEN() {
  gfx->fillRect(0, 50, 240, 229, BLACK);
  
  //Subtítol
  gfx->setTextColor(WHITE);  gfx->setTextSize(3);
  gfx->setCursor(0,70);
  gfx->print("Parametres");
  gfx->drawLine( 0, 98, 180, 98, WHITE);

  //Informació últimes calibracions
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->fillRoundRect(190, 50, 50, 50, 6, BLUE);
  gfx->setCursor(198, 70); gfx->setTextSize(2);
  gfx->println("CAL");
  gfx->drawRoundRect(190, 50, 50, 50, 6, WHITE);
  
  //Paràmetres
    //Temperatura
    gfx->setTextColor(WHITE);  gfx->setTextSize(2);
    gfx->setCursor(0,120);
    gfx->print("Temp. (C):");

    //Sòlids dissolts totals - TDS
    gfx->setCursor(0,155);
    gfx->print("TDS (ppm):");

    //Terbolesa
    gfx->setCursor(0,190);
    gfx->print("TSS (NTU):");

    //Pressió
    gfx->setCursor(0,225);
    gfx->print("P. (KPa):");

    //pH
    gfx->setCursor(0,260);
    gfx->print("pH:");
}

/***************************************************************************
 * @Funció: secondSCREEN()
 * @Resum:  Pantalla que mostra una taula on quedaran recollits els màxims i  
 *          mínims mesurats fins al moment.
 *          
 **************************************************************************/

void secondSCREEN() {
  gfx->fillRect(0, 50, 240, 229, BLACK);
  
  //Subtítol
  gfx->setTextColor(WHITE);  gfx->setTextSize(3);
  gfx->setCursor(0,65);
  gfx->print("Parametres");
  gfx->drawLine( 0, 93, 180, 93, WHITE);

  // Línies de màxims i mínims
  gfx->setTextColor(WHITE);  gfx->setTextSize(2);
  gfx->setCursor(130,100);
  gfx->print("Max  Min");

  //Paràmetres
    //Temperatura
    gfx->setTextColor(WHITE);  gfx->setTextSize(2);
    gfx->setCursor(0,128);
    gfx->print("T.(C):");
    gfx->setCursor(110,128);
    gfx->print(maxT);
    gfx->setCursor(180,128);
    gfx->print(minT);

    //Sòlids dissolts totals - TDS
    gfx->setCursor(0,160);
    gfx->print("TDS(ppm):");
    gfx->setCursor(110,160);
    gfx->print(maxTds);
    gfx->setCursor(180,160);
    gfx->print(minTds);

    //Terbolesa
    gfx->setCursor(0,192);
    gfx->print("TSS(NTU):");
    gfx->setCursor(110,192);
    gfx->print(maxNTU,0);
    gfx->setCursor(180,192);
    gfx->print(minNTU,0);

    //Pressió
    gfx->setCursor(0,224);
    gfx->print("P.(KPa):");
    gfx->setCursor(110,224);
    gfx->print(maxP);
    gfx->setCursor(180,224);
    gfx->print(minP);

    //pH
    gfx->setCursor(0,256);
    gfx->print("pH:");
    gfx->setCursor(110,256);
    gfx->print(maxpH);
    gfx->setCursor(180,256);
    gfx->print(minpH);
}

/***************************************************************************
 * @Funció: Get_Button()
 * @Resum:  Funció que estipula quines zones de la pantalla són botons. És 
 *          roman dins el bucle while sempre que faci menys de tres segons 
 *          que s'ha clicat la pantalla. Si el temps és superior, a través 
 *          d'un seguit de condicionals es determina quin botó de la pantalla 
 *          s'ha clicat.          
 * @Comentari:         
 *          p = ts.getPoint() 
 *              S'obté informació de quina zona ha estat clicada. P és vector
 *              de tres valors. Les dues primeres corresponen a les coordenades  
 *              x i y de la pantalla mentre que el tercer, el z, correspon a la 
 *              força amb què s'ha clicat. 
 *              Els valors de x i y estan compresos entre 0 i 4095. S'escalen 
 *              a les dimensions de la pantalla per poder ser més fàcilment 
 *              interpretats. S'utilitzen la següent funció.
 *                  p.x = map(p.x, TS_MINX, TS_MAXX, 0, gfx->width())
 *              El valor de z està comprès entre 0 i 255. Com major és la inten-
 *              sitat menor valor de z s'obté. S'utilitza un condicional per 
 *              determinar si la pantalla ha estat o no clicada. 
 *                  if ( p.z < 10 || p.z > 140 )
 *              Es prenen de referència els valors de 10 i 140, dins d'aquest 
 *              rang es considera que la pantalla s'ha clicat. 
 *              La pantalla pren el valor de z 129 quan es toca contínuament, 
 *              motiu pel qual es comprova que z sigui diferent aquest valor.
 *              
 **************************************************************************/

int Get_Button() {
  int result;
  
  TS_Point p;
  while (1) { 
    TimeNow = rtc.getSecond() + rtc.getMinute() * 60;
    delay(50);  
    p = ts.getPoint();
  
    if ( abs(TimeNow - TimePressed) >= 3 ) {   
      if ( p.z < 10 || p.z > 140 ) {     
          result = 0;
        }
      else if ( p.z != 129  ) {    
          p.x = map(p.x, TS_MINX, TS_MAXX, 0, gfx->width());
          p.y = map(p.y, TS_MINY, TS_MAXY, 0, gfx->height());
          p.y = 320 - p.y;
  
          // Fletxa enrere "<-"
          if ((p.y > 280) && (p.y < 320) && (p.x > 0) && (p.x < 40)) {
            result = 1; 
            Serial.println(result);
          }
          // Fletxa endavant "->"
          else if ((p.y > 280) && (p.y < 320) && (p.x > 40) && (p.x < 80)) {
            result = 2;
            Serial.println(result);
          }
          // Botó per mostra la informació "I"
          else if((p.y > 0) && (p.y < 50) && (p.x > 190) && (p.x < 240)){
            result = 3;
            Serial.println(result);
          }
          // Botó per guarda les dades registrades a la memòria SD
          else if((p.y > 280) && (p.y < 320) && (p.x > 80) && (p.x < 120)){
            result = 4;
            Serial.println(result);
          }
          // Botó per calibrar el sensor de sòlids dissolts
          else if((p.y > 280) && (p.y < 320) && (p.x > 120) && (p.x < 160)){
            result = 5;
            Serial.println(result);
          }
          // Botó per mostrar la informació de les últimes calibracions
          else if((p.y > 50) && (p.y < 100) && (p.x > 190) && (p.x < 240)){
            result = 6;
            Serial.println(result);
          }
          // Botó per calibrar el sensor de pH 4,0
          else if((p.y > 280) && (p.y < 320) && (p.x > 160) && (p.x < 200)){
            result = 7;
            Serial.println(result);
          }
          // Botó per calibrar el sensor de pH 7,0
          else if((p.y > 280) && (p.y < 320) && (p.x > 200) && (p.x < 240)){
            result = 8;
            Serial.println(result);
          }
          // Botó per seleccionar mode d'operació Local
          else if((p.y > 125) && (p.y < 169) && (p.x > 65) && (p.x < 175)){
            result = 9;
            Serial.println(result);
          }
          // Botó per seleccionar mode d'operació MQTT
          else if((p.y > 180) && (p.y < 224) && (p.x > 65) && (p.x < 175)){
            result = 10;
            Serial.println(result);
          }
          else {
            result = 0;
            Serial.println(result);
          }
          
          if (result != 0) {
            TimePressed = TimeNow; 
          }
      }
      Serial.println(result);
      return result;
    }
  }
}

/***************************************************************************
 * @Funció: displayInfo()
 * @Resum:  Pantalla que mostra informació general sobre el Treball de Fi                   
 *          de Grau (TFG).
 *              
 **************************************************************************/

void displayInfo() {
  gfx->fillRect(190, 50, 50, 50, BLACK);
  gfx->fillRect(0, 65, 240, 211, ORANGE);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setTextColor(BLACK);  
  gfx->setCursor(93 , 90); gfx->setTextSize(3);
  gfx->println("TFG"); 
  gfx->setCursor(12 , 115); gfx->setTextSize(2);
  gfx->println("Treball Fi de Grau");
  PrintCharTFT("Estacio de Control de Qualitat d'Aigua", 6, 145, BLACK, ORANGE, 1);
  PrintCharTFT("Alumne: Joaquim Anglada", 48, 165, BLACK, ORANGE, 1);
  PrintCharTFT("Director: Javier Fernandez", 42, 185, BLACK, ORANGE, 1);
  PrintCharTFT("Codirector: Ruben Mercade", 48, 205, BLACK, ORANGE, 1);
  PrintCharTFT("IQS 2021-2022", 81, 225, BLACK, ORANGE, 1);
  PrintCharTFT("Enginyeria Quimica", 66, 245, BLACK, ORANGE, 1);
}

/***************************************************************************
 * @Funció: displayCAL()
 * @Resum:  Pantalla que mostra la data, hora i valor de k obtingut en l'últi-
 *          ma calibració del sensor de sòlids dissolts. 
 *              
 **************************************************************************/

void displayCAL() {
  gfx->fillRect(190, 50, 50, 50, BLACK);
  gfx->fillRect(0, 65, 240, 211, ORANGE);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setTextColor(BLACK);  
  gfx->setCursor(84 , 100); gfx->setTextSize(3);
  gfx->println("TDS"); 
  gfx->setCursor(18 , 125); gfx->setTextSize(2);
  gfx->println("Ultima calibracio");

  preferences.begin("kValue", false); 
  kValue = preferences.getFloat("kValue", 0.000);
  preferences.begin("DataTDS", false);
  String DataTDS = preferences.getString("DataTDS", "00/00/0000");
  preferences.begin("HoraTDS", false);
  String HoraTDS = preferences.getString("HoraTDS", "00:00:00");
  preferences.end();

  Serial.println("");
  Serial.println("Informació última calibració TDS");
  gfx->setCursor(24 , 165); gfx->setTextSize(2);
  gfx->print("Data: ");
  gfx->println(DataTDS);
  Serial.print("Data: ");
  Serial.println(DataTDS);

  gfx->setCursor(24 , 190); gfx->setTextSize(2);
  gfx->print("Hora: ");
  gfx->println(HoraTDS);
  Serial.print("Hora: ");
  Serial.println(HoraTDS);

  gfx->setCursor(24 , 215); gfx->setTextSize(2);
  gfx->print("Valor K: ");
  gfx->println(kValue,3);
  Serial.print("Valor 'k' actual: ");
  Serial.print(kValue);
}

/***************************************************************************
 * @Funció: displayCALpH4()
 * @Resum:  Pantalla que mostra la data, hora i valor d'A obtingut en l'últi-
 *          ma calibració del sensor de pH amb la dissolució tampó de pH 4,0. 
 *              
 **************************************************************************/

void displayCALpH4(){
  gfx->fillRect(0, 65, 240, 211, ORANGE);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setCursor(66 , 100); gfx->setTextSize(3);
  gfx->println("pH 4.0"); 
  gfx->setCursor(18 , 125); gfx->setTextSize(2);
  gfx->println("Ultima calibracio");

  preferences.begin("aValue", false); 
  aValue = preferences.getFloat("aValue", 0.000);
  preferences.begin("DatapH4", false);
  String DatapH4 = preferences.getString("DatapH4", "00/00/0000");
  preferences.begin("HorapH4", false);
  String HorapH4 = preferences.getString("HorapH4", "00:00:00");
  preferences.end();

  Serial.println("");
  Serial.println("Informació última calibració pH 4,0");
  gfx->setCursor(24 , 165); gfx->setTextSize(2);
  gfx->print("Data: ");
  gfx->println(DatapH4);
  Serial.print("Data: ");
  Serial.println(DatapH4);

  gfx->setCursor(24 , 190); gfx->setTextSize(2);
  gfx->print("Hora: ");
  gfx->println(HorapH4);
  Serial.print("Hora: ");
  Serial.println(HorapH4);

  gfx->setCursor(24 , 215); gfx->setTextSize(2);
  gfx->print("Valor A: ");
  gfx->println(aValue,0);
  Serial.print("Valor 'a' actual: ");
  Serial.print(aValue);
}

/***************************************************************************
 * @Funció: displayCALpH7()
 * @Resum:  Pantalla que mostra la data, hora i valor d'N obtingut en l'últi-
 *          ma calibració del sensor de pH amb la dissolució tampó de pH 7,0. 
 *              
 **************************************************************************/

void displayCALpH7(){
  gfx->fillRect(0, 65, 240, 211, ORANGE);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->fillRect(0, 65, 240, 211, ORANGE);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setCursor(66 , 100); gfx->setTextSize(3);
  gfx->println("pH 7.0"); 
  gfx->setCursor(18 , 125); gfx->setTextSize(2);
  gfx->println("Ultima calibracio");

  preferences.begin("nValue", false); 
  nValue = preferences.getFloat("nValue", 0.000);
  preferences.begin("DatapH7", false);
  String DatapH7 = preferences.getString("DatapH7", "00/00/0000");
  preferences.begin("HorapH7", false);
  String HorapH7 = preferences.getString("HorapH7", "00:00:00");
  preferences.end();

  Serial.println("");
  Serial.println("Informació última calibració pH 7,0");
  gfx->setCursor(24 , 165); gfx->setTextSize(2);
  gfx->print("Data: ");
  gfx->println(DatapH7);
  Serial.print("Data: ");
  Serial.println(DatapH7);

  gfx->setCursor(24 , 190); gfx->setTextSize(2);
  gfx->print("Hora: ");
  gfx->println(HorapH7);
  Serial.print("Hora: ");
  Serial.println(HorapH7);

  gfx->setCursor(24 , 215); gfx->setTextSize(2);
  gfx->print("Valor N: ");
  gfx->println(nValue,0);
  Serial.print("Valor 'n' actual: ");
  Serial.print(nValue);
}

/***************************************************************************
 * @Funció: displaySD()
 * @Resum:  Pantalla que es mostra quan s'està realitzant la còpia de segure- 
 *          tat en la memòria SD. 
 *              
 **************************************************************************/

void displaySD() { 
  gfx->fillRect(190, 50, 50, 50, BLACK);
  gfx->fillRect(0, 65, 240, 211, GREEN);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setTextColor(BLACK); 
  gfx->setCursor(6 , 150); gfx->setTextSize(2);
  gfx->println("Realitzant copia de");
  gfx->setCursor(18 , 175); 
  gfx->println("les mesures en la");
  gfx->setCursor(60 , 200); 
  gfx->println("targeta SD");
}

/***************************************************************************
 * @Funció: displayTDS()
 * @Resum:  Pantalla que es mostra quan s'està realitzant la calibració del 
 *          sensor de sòlids dissolts. Una vegada ha finalitzat s'indica a 
 *          l'usuari. La calibració és dur a terme a través de la funció 
 *          getNewkValue ().
 *              
 **************************************************************************/

void displayTDS() {
  gfx->fillRect(190, 50, 50, 50, BLACK);
  gfx->fillRect(0, 65, 240, 211, GREEN);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setTextColor(BLACK); 
  gfx->setCursor(6 , 100); gfx->setTextSize(2);
  gfx->println("Calibrant el sensor");
  gfx->setCursor(12 , 125); 
  gfx->println("de solids dissolts");
  gfx->setCursor(24 , 170); 
  gfx->println("Dn patro 707 ppm");
  getNewkValue ();
  gfx->setCursor(60 , 205); 
  gfx->println("Calibracio");
  gfx->setCursor(54 , 230); 
  gfx->println("Finalitzada");
}

/***************************************************************************
 * @Funció: displaypH4()
 * @Resum:  Pantalla que es mostra quan s'està realitzant la calibració del 
 *          sensor de pH a dissolució tampó pH 4,0. Una vegada ha finalitzat 
 *          s'indica a l'usuari. La calibració és dur a terme a través de la 
 *          funció getNewaValue ().          
 *                          
 **************************************************************************/

void displaypH4() {
  gfx->fillRect(190, 50, 50, 50, BLACK);
  gfx->fillRect(0, 65, 240, 211, GREEN);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setTextColor(BLACK); 
  gfx->setCursor(6 , 110); gfx->setTextSize(2);
  gfx->println("Calibrant sensor pH");
  gfx->setCursor(30 , 135); 
  gfx->println("Dn patro pH 4.0");
  getNewaValue();
  gfx->setCursor(60 , 195); 
  gfx->println("Calibracio");
  gfx->setCursor(54 , 220); 
  gfx->println("Finalitzada");
}

/***************************************************************************
 * @Funció: displaypH7()
 * @Resum:  Pantalla que es mostra quan s'està realitzant la calibració del 
 *          sensor de pH a dissolució tampó pH 7,0. Una vegada ha finalitzat 
 *          s'indica a l'usuari. La calibració és dur a terme a través de la 
 *          funció getNewnValue ().          
 *                          
 **************************************************************************/

void displaypH7() {
  gfx->fillRect(190, 50, 50, 50, BLACK);
  gfx->fillRect(0, 65, 240, 211, GREEN);
  gfx->drawRect(0, 65, 240, 211, WHITE);
  gfx->setTextColor(BLACK); 
  gfx->setCursor(6 , 110); gfx->setTextSize(2);
  gfx->println("Calibrant sensor pH");
  gfx->setCursor(30 , 135); 
  gfx->println("Dn patro pH 7.0");
  getNewnValue();
  gfx->setCursor(60 , 195); 
  gfx->println("Calibracio");
  gfx->setCursor(54 , 220); 
  gfx->println("Finalitzada");
}

/***************************************************************************
 * @Funció: getNewkValue()
 * @Resum:  Càlcul del valor de k per la calibració del sensor de sòlids           
 *          dissolts. S'inicia de nou la llibreria preferences per poder 
 *          accedir a la memòria flash (espai on s'emmagatzema el valor 
 *          de k). Es mesura el voltatge 50 vegades amb intervals de 100 ms
 *          i posteriorment es calcula la mitjana per eliminar el soroll. 
 *          Finalment, amb un seguit d'operacions es calcula el valor de k i
 *          s'emmagatzema en la memòria flash. 
 *          Equacions extretes de la pàgina web del fabricant:
 *            https://wiki.dfrobot.com/Gravity__Analog_TDS_Sensor___Meter_
 *            For_Arduino_SKU__SEN0244 
 * @Comentari:                         
 *          voltage = ( sumPreVoltage / samples ) * ( 0.1875 / 1000 )
 *              El voltatge s'obté de multiplicar la lectura en bits de l'ADC  
 *              per 0,1875 mV que és equivalent a 1 bit. El factor de 1000
 *              es fa servir per passar de mV a V.
 **************************************************************************/

void getNewkValue() {
  Serial.println("Calibració Sòlids dissolts");
  preferences.begin("kValue", false);
  kValue = preferences.getFloat("kValue", 0.000);
  Serial.printf("kValue actual: %.3f", kValue);
  
  const int DnPatro = 707;
  int contador = 0;
  const int samples = 50;
  unsigned long  sumPreVoltage = 0;
  //long preVoltage = 0; 
  
  while (contador<samples) {
    adc1 = ads.getConversionResults(channel1);;
    sumPreVoltage += adc1; 
    delay(100);
    contador++;
  }
  voltage = ( sumPreVoltage / samples ) * ( 0.1875 / 1000 );
  ecValue25 = DnPatro / TdsFactor;
  ecValue = ecValue25 * ( 1.0 + 0.02 * ( temperatureC - 25.0 ) ); 
  kValue = ecValue / ( 133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage );
  
  preferences.putFloat("kValue", kValue);
  Serial.printf(" -> %.3f\n", kValue);

  preferences.begin("DataTDS", false);
  String DataTDS = preferences.getString("DataTDS", "00/00/0000");
  Serial.print("Data última calibració TDS: ");
  Serial.print(DataTDS);
  DataTDS = rtc.getTime("%d/%m/%y");
  preferences.putString("DataTDS", DataTDS);
  Serial.print(" -> ");
  Serial.println(DataTDS);

  preferences.begin("HoraTDS", false);
  String HoraTDS = preferences.getString("HoraTDS", "00:00:00");
  Serial.print("Hora última calibració TDS: ");
  Serial.print(HoraTDS);
  HoraTDS = rtc.getTime("%H:%M:%S");
  preferences.putString("HoraTDS", HoraTDS);
  Serial.print(" -> ");
  Serial.println(HoraTDS);
  
  preferences.end();
}

/***************************************************************************
 * @Funció: getNewaValue()
 * @Resum:  Càlcul del valor de a per la calibració del sensor de pH amb            
 *          dissolució tampó de pH 4,0. S'inicia de nou la llibreria pre-
 *          ferences per poder accedir a la memòria flash (espai on s'emma-
 *          gatzema el valor de a). Es mesura el voltatge 50 vegades amb 
 *          intervals de 100 ms i posteriorment es calcula la mitjana per 
 *          eliminar el soroll. Finalment, s'emmagatzema en la memòria flash. 
 * @Comentari:                         
 *          adc3 = analogRead(pHSensorPin) * 3.3 / 4096 * 1000
 *              El voltatge s'obté de multiplicar la lectura en bits de l'ADC  
 *              pel coeficient del màxim voltatge permés per l'ADC 3,3 V i 4096  
 *              bits. El factor de 1000 es fa servir per passar de V a mV.
 *              
 **************************************************************************/

void getNewaValue() {
  Serial.println("Calibració sensor pH 4,0");
  preferences.begin("aValue", false);
  aValue = preferences.getFloat("aValue", 0.000);
  Serial.printf("aValue actual: %.3f", aValue);
  
  int contador = 0;
  const int samples = 50;
  unsigned long  sumPreVoltage = 0; 
  
  while (contador<samples) {
    adc3 = analogRead(pHSensorPin) * 3.3 / 4096 * 1000;
    sumPreVoltage += adc3; 
    delay(100);
    contador++;
  }

  Serial.print("aValue: ");
  Serial.print(aValue);
  aValue = ( sumPreVoltage / samples );
  
  preferences.putFloat("aValue", aValue);
  Serial.printf(" -> %.3f\n", aValue);

  preferences.begin("DatapH4", false);
  String DatapH4 = preferences.getString("DatapH4", "00/00/0000");
  Serial.print("Data última calibració pH 4,0: ");
  Serial.print(DatapH4);
  DatapH4 = rtc.getTime("%d/%m/%y");
  preferences.putString("DatapH4", DatapH4);
  Serial.print(" -> ");
  Serial.println(DatapH4);

  preferences.begin("HorapH4", false);
  String HorapH4 = preferences.getString("HorapH4", "00:00:00");
  Serial.print("Hora última calibració pH 4,0: ");
  Serial.print(HorapH4);
  HorapH4 = rtc.getTime("%H:%M:%S");
  preferences.putString("HorapH4", HorapH4);
  Serial.print(" -> ");
  Serial.println(HorapH4);
  
  preferences.end();
}

/***************************************************************************
 * @Funció: getNewnValue()
 * @Resum:  Càlcul del valor de a per la calibració del sensor de pH amb            
 *          dissolució tampó de pH 7,0. S'inicia de nou la llibreria pre-
 *          ferences per poder accedir a la memòria flash (espai on s'emma-
 *          gatzema el valor de n). Es mesura el voltatge 50 vegades amb 
 *          intervals de 100 ms i posteriorment es calcula la mitjana per 
 *          eliminar el soroll. Finalment, s'emmagatzema en la memòria flash. 
 * @Comentari:                         
 *          adc3 = analogRead(pHSensorPin) * 3.3 / 4096 * 1000
 *              El voltatge s'obté de multiplicar la lectura en bits de l'ADC  
 *              pel coeficient del màxim voltatge permés per l'ADC 3,3 V i 4096  
 *              bits. El factor de 1000 es fa servir per passar de V a mV.
 *              
 **************************************************************************/

void getNewnValue() {
  Serial.println("Calibració sensor pH 7,0");
  preferences.begin("nValue", false);
  nValue = preferences.getFloat("nValue", 0.000);
  Serial.printf("nValue actual: %.3f", nValue);
  
  int contador = 0;
  const int samples = 50;
  unsigned long  sumPreVoltage = 0; 
  
  while (contador<samples) {
    adc3 = analogRead(pHSensorPin) * 3.3 / 4096 * 1000;
    sumPreVoltage += adc3; 
    delay(100);
    contador++;
  }

  Serial.print("nValue: ");
  Serial.print(nValue);
  nValue = ( sumPreVoltage / samples );
  
  preferences.putFloat("nValue", nValue);
  Serial.printf(" -> %.3f\n", nValue);

  preferences.begin("DatapH7", false);
  String DatapH7 = preferences.getString("DatapH7", "00/00/0000");
  Serial.print("Data última calibració pH 7,0: ");
  Serial.print(DatapH7);
  DatapH7 = rtc.getTime("%d/%m/%y");
  preferences.putString("DatapH7", DatapH7);
  Serial.print(" -> ");
  Serial.println(DatapH7);

  preferences.begin("HorapH7", false);
  String HorapH7 = preferences.getString("HorapH7", "00:00:00");
  Serial.print("Hora última calibració pH 7,0: ");
  Serial.print(HorapH7);
  HorapH7 = rtc.getTime("%H:%M:%S");
  preferences.putString("HorapH7", HorapH7);
  Serial.print(" -> ");
  Serial.println(HorapH7);
  
  preferences.end();
}

/***************************************************************************
 * @Funció: getTDS ()
 * @Resum:  Càlcul dels sòlids dissolts, equacions extretes de la pàgina web 
 *          del fabricant: https://wiki.dfrobot.com/Gravity__Analog_TDS_Sensor
 *          ___Meter_For_Arduino_SKU__SEN0244
 *              
 **************************************************************************/

int getTDS () {
  adc1 = ads.getConversionResults(channel1);
  voltage = (adc1*0.1875)/1000;
  ecValue = ( 133.42 * voltage * voltage * voltage - 255.86 * voltage * voltage + 857.39 * voltage ) * kValue;
  ecValue25  =  ecValue / ( 1.0 + 0.02 * ( temperatureC - 25.0 ) );  //temperature compensation
  tdsValue = ecValue25 * TdsFactor;

  if (tdsValue > 2000) {
    tdsValue = 2000;
  }
  return tdsValue;  
}

/***************************************************************************
 * @Funció: getTSS ()
 * @Resum:  Càlcul de la terbolesa, equacions obtingudes experimentalment.
 *              
 **************************************************************************/

int getTSS() {
  adc2 = ads.getConversionResults(channel2);
  voltageTSS = (adc2*0.1875)/1000;
  
  if(voltageTSS < 1.641494){ 
      NTU = 3000;
    }
    else if (voltageTSS > 3.81917) { 
      NTU = 0;
    }
    else{
      NTU = - 3381.44125 * voltageTSS -10.72595 * temperatureC + 1.72407 * voltageTSS * temperatureC + 369.23536 * voltageTSS * voltageTSS + 7666.50096; 
    }
    Serial.print("NTU: ");
    Serial.println(NTU);
    return NTU;
}

/***************************************************************************
 * @Funció: getpH ()
 * @Resum:  Càlcul del pH, equacions extretes de la pàgina web del fabricant: 
 *          https://www.dfrobot.com/product-2069.html
 *              
 **************************************************************************/

float getpH(){
  int contador = 0;
  const int samples = 50;
  unsigned long  sumPreVoltage = 0;
  
  while (contador<samples) {
    adc3 = analogRead(pHSensorPin) * 3.3 / 4096 * 1000;
    sumPreVoltage += adc3; 
    delay(50);
    contador++;
  }

  adc3 = sumPreVoltage / samples;
  pendent = (7.0-4.0)/((nValue-1500)/3.0-(aValue-1500)/3.0);
  ordenada = 7.0-pendent*(nValue-1500)/3.0;
  pH = pendent*(adc3-1500)/3.0+ordenada;
  
    if (pH >= 14.00){
    pH = 14.00;
  }
  else if ( pH <= 0.00){
    pH = 0.00;
  }
  return pH;
}

/***************************************************************************
 * @Funció: reconnect()
 * @Resum:  Funció per reconnectar-se al broker en cas de pèrdua de connexió 
 *          i rebre publicacions en cas que l'estació estigui subscrita.          
 *              
 **************************************************************************/

void reconnect() {
  while (!client.connected()) {
    Serial.print("Iniciant la connexió amb el Broker");
    String clienteId = "ESP32";
    if (client.connect(clienteId.c_str())) {
      Serial.println(" Connectat");
      client.subscribe("esp32iot");                  
    }
    else {
      Serial.print("Ha fallat, rc=");
      Serial.print(client.state());
      Serial.println(" esperant 3 segons");
      delay(3000);
    }
  }
}

/***************************************************************************
 * @Funció: callback ()
 * @Resum:  Funció per llegir els missatges enviats pel broker a l'ESP32,
 *          actualment en desús. 
 *              
 **************************************************************************/

void callback(String topic, byte* message, unsigned int length) { 
  Serial.print("Missatge que arriba del topic: ");
  Serial.print(topic);
  Serial.print(" . Missatge: ");

  for(int i=0; i<length; i++){
    Serial.print((char)message[i]);
    messageData += (char)message[i];
  }
  Serial.println();
}
