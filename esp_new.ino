// ESP32 (normal board) - car controller
// Laptop se WiFi pe command leta hai: /cmd?c=F | L | R | T | S
// F = aage, L = left, R = right, T = search (ghoomo), S = stop
// Ultrasonic safety: aage 20 cm se kam ho to F nahi chalega

#include <WiFi.h>
#include <WebServer.h>

const char* SSID = "og";
const char* PASS = "00000000";

// L298N pins
#define IN1 26
#define IN2 27
#define IN3 25
#define IN4 33
#define ENA 14
#define ENB 32

// HC-SR04 pins (ECHO ko voltage divider se jodna)
#define TRIG 5
#define ECHO 18

int SPEED = 150;        // seedha chalne ki speed (0-255)
int TURN  = 130;        // ghoomne ki speed
int STOP_CM = 20;       // itne paas aaye to ruk jao

WebServer server(80);
char cmd = 'S';
unsigned long lastCmd = 0;
unsigned long lastPing = 0;
long dist = 999;
unsigned long lastWifiTry = 0;
bool wasUp = true;

void pwm(int pin, int val) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, val);
#else
  ledcWrite(pin == ENA ? 0 : 1, val);
#endif
}

void setMotor(int a, int b, int en, int spd) {
  digitalWrite(a, spd > 0);
  digitalWrite(b, spd < 0);
  pwm(en, abs(spd));
}

void drive(int l, int r) {
  setMotor(IN1, IN2, ENA, l);
  setMotor(IN3, IN4, ENB, r);
}

// Chhota-chhota jhatke se ghoomo: camera ko ruka hua frame milta hai
// dir = -1 left, +1 right
void pulseTurn(int dir, int onMs, int offMs) {
  unsigned long t = millis() % (onMs + offMs);
  if (t < onMs) drive(dir * TURN, -dir * TURN);
  else drive(0, 0);
}

long readDist() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long d = pulseIn(ECHO, HIGH, 25000);
  if (d == 0) return 999;
  return d * 0.034 / 2;
}

void handleCmd() {
  if (server.hasArg("c")) {
    cmd = server.arg("c")[0];
    lastCmd = millis();
  }
  server.send(200, "text/plain", "OK");
}

void handleDist() {
  server.send(200, "text/plain", String(dist));
}

void setup() {
  Serial.begin(115200);

  int pins[] = {IN1, IN2, IN3, IN4, TRIG};
  for (int p : pins) pinMode(p, OUTPUT);
  pinMode(ECHO, INPUT);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(ENA, 1000, 8);
  ledcAttach(ENB, 1000, 8);
#else
  ledcSetup(0, 1000, 8); ledcAttachPin(ENA, 0);
  ledcSetup(1, 1000, 8); ledcAttachPin(ENB, 1);
#endif

  drive(0, 0);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // WiFi power-saving band: disconnect kam hota hai
  WiFi.setAutoReconnect(true);
  WiFi.begin(SSID, PASS);
  Serial.print("WiFi connect ho raha hai");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());   // ye IP Python script me ROBOT me daalna

  server.on("/cmd", handleCmd);
  server.on("/dist", handleDist);   // browser me check karo: sensor sahi chal raha hai ya nahi
  server.begin();
}

void loop() {
  // WiFi toot jaye to car ruk jaye aur har 5 sec me dobara judne ki koshish kare
  bool up = (WiFi.status() == WL_CONNECTED);
  if (!up) {
    drive(0, 0);
    if (millis() - lastWifiTry > 5000) {
      lastWifiTry = millis();
      Serial.println("WiFi gaya, dobara judne ki koshish...");
      WiFi.disconnect();
      WiFi.begin(SSID, PASS);
    }
    wasUp = false;
    return;
  }
  if (!wasUp) {
    Serial.print("WiFi wapas juda, IP: ");
    Serial.println(WiFi.localIP());   // IP badla ho to Python script me ROBOT update karo
    wasUp = true;
  }

  server.handleClient();

  if (millis() - lastPing > 60) {
    dist = readDist();
    lastPing = millis();
  }

  // Failsafe: 0.5 sec se command nahi aaya to ruk jao
  if (millis() - lastCmd > 500) cmd = 'S';

  // Safety override: aage obstacle ho to F band
  if (cmd == 'F' && dist < STOP_CM) cmd = 'S';

  switch (cmd) {
    case 'F': drive(SPEED, SPEED);        break;
    case 'L': pulseTurn(-1, 120, 180);    break;
    case 'R': pulseTurn(+1, 120, 180);    break;
    case 'T': pulseTurn(-1, 200, 500);    break;  // search
    default:  drive(0, 0);
  }
}
