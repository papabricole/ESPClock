/*
Chip is ESP8266EX
Features: WiFi
Crystal is 26MHz
MAC: 2c:3a:e8:4a:d3:38

Flash real id:   0014301C
Flash real size: 1048576 bytes

Flash ide  size: 1048576 bytes
Flash ide speed: 40000000 Hz
Flash ide mode:  DIO
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <LedControl.h>
#include <NTPClient.h>
#include <PubSubClient.h>

#define BUZZER_PIN 16
#define DHT11_PIN 5
#define PHOTODIODE_PIN A0
#define BUTTON_PIN 0
#define CS_PIN 4
#define MOSI_PIN 13
#define CLK_PIN 14

WiFiUDP NtpUDP;
DHT Dht11(DHT11_PIN, DHT11);
LedControl Display(MOSI_PIN, CLK_PIN, CS_PIN, 1);
NTPClient TimeClient(NtpUDP, "europe.pool.ntp.org", 3600, 60000);

void MqttCallback(char *topic, byte *payload, unsigned int length);

WiFiClient espClient;
PubSubClient mqtt("quadra.local", 1883, MqttCallback, espClient);

int temperature = 0;

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting");

    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);
    wifiManager.autoConnect("ESPClock");

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Connection Failed! Rebooting...");
        delay(5000);
        ESP.restart();
    }

    ArduinoOTA.setHostname("ESPClock");
    ArduinoOTA.begin();

    Dht11.begin();

    Display.shutdown(0, false);
    Display.setIntensity(0, 0);
    Display.clearDisplay(0);

    TimeClient.begin();
}

void setChar(int digit, char value, bool dp)
{
    switch (digit)
    {
    case 0:
        Display.setChar(0, 0, value, dp);
        break;
    case 1:
        Display.setChar(0, 3, value, dp);
        break;
    case 2:
        // this one is upside down (on purpose)
        Display.setCharInv(0, 2, value, dp);
        break;
    case 3:
        Display.setChar(0, 1, value, dp);
        break;
    }
}

void displayTime()
{
    const int h = TimeClient.getHours();
    const int m = TimeClient.getMinutes();

    setChar(0, h / 10, false);
    setChar(1, h % 10, true);
    setChar(2, m / 10, true);
    setChar(3, m % 10, false);
}
#if USE_DHT11
void displayTemperature()
{
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow
    // sensor)
    const int h = Dht11.readHumidity();
    // Read temperature as Celsius (the default)
    const int t = Dht11.readTemperature();

    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t))
    {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }

    setChar(0, t / 10, false);
    setChar(1, t % 10, false);
    setChar(2, 'C', true);
    setChar(3, ' ', false);
}
#else
void displayTemperature()
{
    if (temperature < 0)
    {
        setChar(0, '-', false);
        setChar(1, abs(temperature), false);
    }
    else
    {
        setChar(0, (temperature / 10) != 0 ? temperature / 10 : ' ', false);
        setChar(1, temperature % 10, false);
    }
    setChar(2, 'C', true);
    setChar(3, ' ', false);
}
#endif
void updateBrightness()
{
    static unsigned long lastReadTime = millis();
    static int currentValue = -1;

    const unsigned long elapsed = millis() - lastReadTime;
    if (elapsed < 500)
        return;

    const int ambientLight = 1024 - analogRead(PHOTODIODE_PIN);

    if (currentValue != ambientLight)
    {
        Display.setIntensity(0, ambientLight / 1024.f * 15);
        currentValue = ambientLight;
    }
    lastReadTime = millis();
}

void updateDisplay()
{
    static unsigned long startTime = millis();
    static int mode = 0;

    const unsigned long elapsed = millis() - startTime;

    if (mode % 2 == 0)
    {
        displayTime();
        if (elapsed > 5000)
        {
            mode++;
            startTime = millis();
        }
    }
    else
    {
        displayTemperature();
        if (elapsed > 2000)
        {
            mode++;
            startTime = millis();
        }
    }
}

void MqttCallback(char *topic, byte *payload, unsigned int length)
{
#if 0
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();
#endif
    temperature = atoi((const char *)payload);

    // single digit for negative values....
    if (temperature < -9)
        temperature = -9;
}

void updateMqtt()
{
    if (!mqtt.connected())
    {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ESPClock-" + String(random(0xffff), HEX);

        // Attempt to connect
        if (mqtt.connect(clientId.c_str(), "mqtt", "mqtt"))
        {
            Serial.println("connected");
            mqtt.subscribe("sensors/accuweather/temperature");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(mqtt.state());
        }
    }
    else
    {
        mqtt.loop();
    }
}

void loop()
{
    ArduinoOTA.handle();
    TimeClient.update();

    updateMqtt();

    updateBrightness();

    updateDisplay();
}