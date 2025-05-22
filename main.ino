#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>

#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const char* topic = "test/wokwi/matlab";

WiFiClient espClient;
PubSubClient client(espClient);

RTC_DS1307 rtc;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

uint8_t emergency = 32, normal = 33, warning = 25;
uint8_t b1 = 26, b2 = 27, b3 = 14, b4 = 12, linAc = 17;
unsigned long prevInst = 0, prevInst2 = 0;
uint8_t b1State = 0, b2State = 0, b3State = 0, b4State = 0;

uint8_t scaleDown = 1;
uint8_t dispenseMed = 0;
uint8_t artHour = 0, artDay = 0;
uint8_t actuators[] = {18, 5, 17};

const char* dayNames[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

void display(int x, int y, int size, String data) {
  oled.setTextSize(size);
  oled.setTextColor(WHITE);
  oled.setCursor(x, y);
  oled.println(data);
  oled.display();
}

class Med {
  public:
    uint8_t id, active, in_a_day, dosage_time[3];
    bool week_days[7];
    Med() {
      active = 0;
      in_a_day = 1;
      for (int i = 0; i < 3; i++) dosage_time[i] = 0;
      for (int i = 0; i < 7; i++) week_days[i] = false;
    }
};

Med med_entry[3];
Med pendingMed;

class PillSense {
  public:
    uint8_t mode, pulse, meds, med_view, med_id;
    uint8_t active_function;
    uint8_t cursor, cursor_max;
    int8_t prev_function;
    uint8_t config_stage = 0;
    uint8_t med_cursor = 0;
    uint8_t time_slot = 0;
    uint8_t selected_hour = 0;
    uint8_t day_cursor = 0, med_set;
    bool day_config_done = false;

    PillSense() {
      active_function = 0;
      prev_function = -1;
      cursor = 1;
      cursor_max = 4;
      pulse = 1;
      meds = 0;
      med_view = 0;
      med_set = 0;
    }

    void configure() {
      oled.clearDisplay();
      if (config_stage == 0) {
        display(10, 0, 1, "Dosage per Day:");
        display(40, 20, 2, String(med_entry[med_cursor].in_a_day));
      } else if (config_stage == 1) {
        if (day_cursor <= 6) {
          display(10, 0, 1, "Select Days:");
          display(30, 20, 2, dayNames[day_cursor]);
        } else {
          complete_day_config();
        }
      } else if (config_stage == 2) {
        display(10, 0, 1, "Set Dose Time");
        display(20, 20, 2, (selected_hour < 10 ? "0" : "") + String(selected_hour) + ":00");
        display(10, 50, 1, "Dose #" + String(time_slot + 1));
      }
      oled.display();
    }

    void complete_day_config() {
      med_entry[med_cursor].active = 1;
      display(10, 25, 2, "Type " + String(med_cursor + 1));
      display(10, 45, 2, "is Set!");
      oled.display();
      delay(2000);
      config_stage = 0;
      time_slot = 0;
      selected_hour = 0;
      day_cursor = 0;
      day_config_done = false;
      active_function = 0;
      cursor = 1;
    }

    void increment_config() {
      if (config_stage == 1) {
        if (day_cursor <= 6) {
          med_entry[med_cursor].week_days[day_cursor] = true;
        }
      } else if (config_stage == 2) {
        med_entry[med_cursor].dosage_time[time_slot] = selected_hour;
        time_slot++;
        if (time_slot >= med_entry[med_cursor].in_a_day) {
          med_entry[med_cursor].active = 1;
          display(30, 10, 2, "Type " + String(med_cursor + 1));
          display(30, 40, 2, "is Set!");
          med_set = 1;
          String temp = "Type - "+String(med_cursor + 1) + String(" Setup");
          const char *  pubMsg = temp.c_str();
          Serial.println(pubMsg);
          client.publish(topic, pubMsg);
          oled.display();
          delay(2000);
          config_stage = 0;
          time_slot = 0;
          selected_hour = 0;
          active_function = 0;
          cursor = 1;
        } else {
          selected_hour = 0;
        }
      } else {
        config_stage++;
      }
      pulse = 1;
    }

    void inc_value() {
      if (config_stage == 0) {
        if (med_entry[med_cursor].in_a_day < 3) med_entry[med_cursor].in_a_day++;
      } else if (config_stage == 1) {
        if (day_cursor < 6) day_cursor++;
        else {
          day_cursor++;
          config_stage++;
        }
      } else if (config_stage == 2) {
        if (selected_hour < 23) selected_hour++;
        else selected_hour = 0;
      }
      pulse = 1;
    }

    void dec_value() {
      if (config_stage == 0) {
        if (med_entry[med_cursor].in_a_day > 1) med_entry[med_cursor].in_a_day--;
      } else if (config_stage == 1) {
        if (day_cursor > 0) day_cursor--;
      } else if (config_stage == 2) {
        if (selected_hour > 0) selected_hour--;
        else selected_hour = 23;
      }
      pulse = 1;
    }

    void view_medication() {
      oled.clearDisplay();
      if(cursor == 4){
        cursor = 1;
        active_function = 0;
        pulse = 1;
      }
      else{
        Med m = med_entry[cursor - 1];
        if (!m.active) {
          display(30, 10, 2, "Type-" + String(cursor));
          display(25, 40, 2, "Not Set");
        } else {
          displayHighlighted(1, 1, 1, "View > Type " + String(cursor));
          display(0, 10, 1, "---------------------");
          display(0, 15, 1, "Dosages: " + String(m.in_a_day));
          String days = "Days: ";
          for (int i = 0; i < 7; i++) {
            if (m.week_days[i]) days += String(dayNames[i]) + "/";
          }
          display(0, 27, 1, days);
          String times = "Time: ";
          for (int i = 0; i < m.in_a_day; i++) {
            times += (m.dosage_time[i] < 10 ? "0" : "") + String(m.dosage_time[i]) + ":00 ";
          }
          display(0, 39, 1, times);
        }
        oled.display();
      }
    }

    void run() {
      if (pulse) {
        if (active_function == 0) function_0();
        else if (active_function == 1) function_1();
        else if (active_function == 2) function_2();
        else if (active_function == 3) configure();
        else if (active_function == 4) view_medication();
        pulse = 0;
      }
    }

    void update_cursor() {
      cursor++;
      if (cursor > cursor_max) cursor = 1;
    }

    void dec_cursor() {
      pulse = 1;
      if (cursor > 1) cursor--;
    }

    void inc_cursor() {
      pulse = 1;
      if (cursor < cursor_max) cursor++;
    }

    void implement(uint8_t captured_cursor, uint8_t function) {
      if (function == 1) {
        if (captured_cursor == 1) active_function = 2;
        else if (captured_cursor == 2) active_function = 4;
        else if (captured_cursor == 3) {
          for (int i = 0; i < 3; i++) med_entry[i] = Med();
          active_function = 0;
          //med_set = 1;
        } else if (captured_cursor == 4) {
          active_function = 0;
        }
        cursor = 1;
        pulse = 1;
      } else if (function == 2) {
        med_cursor = captured_cursor - 1;
        active_function = 3;
        pulse = 1;
        config_stage = 0;
        day_cursor = 0;
        med_entry[med_cursor] = Med();
      } else if (function == 4) {
        cursor = captured_cursor;
        pulse = 1;
      }
    }
};

PillSense pSense;

void setup() {
  Serial.begin(9600);
  Wire.begin();

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("failed to start SSD1306 OLED"));
    while (1);
  }

  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (!rtc.isrunning()) rtc.adjust(DateTime(2024, 1, 1, 0, 0, 0));

  pinMode(emergency, OUTPUT);
  pinMode(normal, OUTPUT);
  pinMode(warning, OUTPUT);


  digitalWrite(normal, HIGH);
  for(int i=0; i<3; i++) pinMode(actuators[i], OUTPUT);
  for(int i=0; i<3; i++) digitalWrite(actuators[i], LOW);

  pinMode(b1, INPUT);
  pinMode(b2, INPUT);
  pinMode(b3, INPUT);
  pinMode(b4, INPUT);

  //delay(2000);
  oled.clearDisplay();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(100);
  client.setServer(mqtt_server, 1883);
  //display(0, 2, 1, "Robotronix");
}

void loop() {
  if (!client.connected()) {
    while (!client.connect("wokwiClient")) delay(1000);
  }
  DateTime now = rtc.now();

  if(scaleDown == 1){
    if(millis() - prevInst2 >= 10000){
      artHour++;
      if(artHour >=24){
        artDay ++;
        artHour = 0;
      }
      Serial.println("Current Hour: " + String(artHour) + ":00  , Day: " + dayNames[artDay]);
      prevInst2 = millis();
    }

    if (pSense.med_set) {
    //Serial.println("Entered!");
    for(int i=0; i<3; i++){
      if(med_entry[i].active){
        if(med_entry[i].week_days[artDay]){
          for(int j=0; j<3; j++){
            if(pendingMed.active){
              if((pendingMed.dosage_time[0] > med_entry[i].dosage_time[j]) && med_entry[i].dosage_time[j] != 0)
              {
                pendingMed.id = i;
                pendingMed.active = 1;
                pendingMed.dosage_time[0] = med_entry[i].dosage_time[j];
                //Serial.println(String(j) + ": " + String(med_entry[i].dosage_time[j]));
              }
            }
            else{
              pendingMed.id = i;
              pendingMed.active = 1;
              pendingMed.dosage_time[0] = med_entry[i].dosage_time[0];
              //Serial.println(pendingMed.dosage_time[0]);
              //Serial.println(med_entry[i].dosage_time[0]);
            }
            pSense.pulse = 1;
          }
        }
        med_entry[pendingMed.id].week_days[artDay] = false;
      }
    }
    pSense.med_set = 0;

    //prevInst2 = millis();
  }

    if(pendingMed.active){
      if(pendingMed.week_days[0] == artDay){
        if(pendingMed.dosage_time[0] == artHour){
          dispenseMed = 1;
          pendingMed.active = 0;
          pSense.pulse = 1;
        digitalWrite(warning, HIGH);
        digitalWrite(normal, LOW);
        }
      }
    }
  }
  else{
    if (pSense.med_set) {
    //Serial.println("Entered!");
    for(int i=0; i<3; i++){
      if(med_entry[i].active){
        if(med_entry[i].week_days[now.dayOfTheWeek() - 1]){
          for(int j=0; j<3; j++){
            if(pendingMed.active){
              if((pendingMed.dosage_time[0] > med_entry[i].dosage_time[j]) && med_entry[i].dosage_time[j] != 0)
              {
                pendingMed.id = i;
                pendingMed.active = 1;
                pendingMed.dosage_time[0] = med_entry[i].dosage_time[j];
                //Serial.println(String(j) + ": " + String(med_entry[i].dosage_time[j]));
              }
            }
            else{
              pendingMed.id = i;
              pendingMed.active = 1;
              pendingMed.dosage_time[0] = med_entry[i].dosage_time[0];
              //Serial.println(pendingMed.dosage_time[0]);
              //Serial.println(med_entry[i].dosage_time[0]);
            }
            pSense.pulse = 1;
          }
        }
      }
    }
    pSense.med_set = 0;

    //prevInst2 = millis();
  }

    if(pendingMed.active){
        if(pendingMed.dosage_time[0] == now.hour()){
          dispenseMed = 1;
          pendingMed.active = 0;
        }
    }
  }

  // if(Serial.available()){
  //   char c = Serial.read();
  //   if(c == '1'){
  //     Serial.println("Data Sent!");
  //     client.publish(topic, "Type - 1");
  //   }
  //   else if(c == '2'){
  //     client.publish(topic, "Type - 2");
  //   }
  //   else if(c == '3'){
  //     client.publish(topic, "Type - 3");
  //   }
  // }



  
  //Serial.println(now.dayOfTheWeek());

  b1State = digitalRead(b1);
  b2State = digitalRead(b2);
  b3State = digitalRead(b3);
  b4State = digitalRead(b4);

  if ((millis() - prevInst >= 100) && (b1State || b2State || b3State || b4State)) {
    oled.clearDisplay();
    if (b1State) {
      b1State = 0;
      pSense.active_function++;
      if (pSense.active_function > 4) pSense.active_function = 0;
      pSense.pulse = 1;
      //client.publish(topic, "Type - 1");
    }
    if (b2State) {
      b2State = 0;
      if(dispenseMed == 1){

        digitalWrite(actuators[pendingMed.id], HIGH);
        String temp = "Type - "+String(pendingMed.id + 1);
        const char *  pubMsg = temp.c_str();
        Serial.println(pubMsg);
        client.publish(topic, pubMsg);
        delay(3000);
        digitalWrite(warning, LOW);
        digitalWrite(normal, HIGH);
        digitalWrite(actuators[pendingMed.id], LOW);
        dispenseMed = 0;
        pSense.pulse = 1;
        pendingMed = Med();
        pSense.med_set = 1;
        

      }
      else{
        if (pSense.active_function == 3) pSense.increment_config();
        else pSense.implement(pSense.cursor, pSense.active_function);
      }
    }
    if (b3State) {
      b3State = 0;
      if (pSense.active_function == 3) pSense.dec_value();
      else pSense.dec_cursor();
    }
    if (b4State) {
      b4State = 0;
      if (pSense.active_function == 3) pSense.inc_value();
      else pSense.inc_cursor();
    }
    prevInst = millis();
  }

  pSense.run();
}

void function_0() {
  oled.clearDisplay();
  displayHighlighted(1, 1, 1, "PillSense");
  display(0, 10, 1, "---------------------");
  DateTime now = rtc.now();
  String str = String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute());
  //display(0, 0, 1, "PillSense");
  display(30, 25, 2, str);
  if(pendingMed.active) displayHighlighted(15, 50, 1, "Type-"+ String(pendingMed.id + 1) 
  + " at " + String(pendingMed.dosage_time[0]) + ":00");
  else if(dispenseMed == 1){
    displayHighlighted(10, 50, 1, "Ready to Dispense");
  }
  else displayHighlighted(25, 50, 1, "No Schedule");
}

void function_1() {
  oled.clearDisplay();
  //display(30, 0, 2, "Menu");
  displayHighlighted(1, 1, 1, "PillSense > Menu");
  display(0, 10, 1, "---------------------");
  display(10, 15, 1, "Configure");
  display(10, 30, 1, "View");
  display(10, 45, 1, "Reset");
  display(10, 60, 1, "Exit");
  display(0, 15 * pSense.cursor, 1, ">");
  oled.display();
}

void function_2() {
  oled.clearDisplay();
  //display(30, 0, 2, "Meds");
  displayHighlighted(1, 1, 1, "Menu > Meds");
  display(0, 10, 1, "---------------------");
  display(10, 15, 1, "Type 1");
  display(10, 30, 1, "Type 2");
  display(10, 45, 1, "Type 3");
  display(0, 15 * pSense.cursor, 1, ">");
  oled.display();
}

void displayHighlighted(int x, int y, int size, String data) {
  int16_t x1, y1;
  uint16_t w, h;
  oled.setTextSize(size);
  oled.getTextBounds(data, x, y, &x1, &y1, &w, &h);

  // Draw white background rectangle
  oled.fillRect(x1 - 1, y1 - 1, w + 2, h + 2, WHITE);

  // Set text color to black to create contrast
  oled.setTextColor(BLACK);
  oled.setCursor(x, y);
  oled.println(data);

  // Reset color to white for future prints
  oled.setTextColor(WHITE);
  oled.display();
}
