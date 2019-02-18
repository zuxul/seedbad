
// Термометр HTU21D
// Connect Vin to 3-5VDC (зеленый)
// Connect GND to ground (коричневый)
// Connect SCL to I2C clock pin (A5 on UNO) (синий)
// Connect SDA to I2C data pin (A4 on UNO) (оранжевый)

#include <Wire.h>
#include "Adafruit_HTU21DF.h"
Adafruit_HTU21DF htu = Adafruit_HTU21DF();

// Дисплей LCD 1602
// LCD RS pin to digital pin 12
// LCD Enable pin to digital pin 11
// LCD D4 pin to digital pin 2
// LCD D5 pin to digital pin 3
// LCD D6 pin to digital pin 4
// LCD D7 pin to digital pin 5
// LCD R/W pin to ground
// 10K resistor:
// ends to +5V and ground
// wiper to LCD VO pin (pin 3)
#include <LiquidCrystal.h>
LiquidCrystal lcd(12, 11, 2, 3, 4, 5);

// Кнопки
#include <OneButton.h>
OneButton selectBtn(6, true);
OneButton plusBtn(7, true);
OneButton minusBtn(8, true);

// твердотельное реле
#define RELE_PIN 9

enum StateView {
  MAIN_VIEW // показываем температуру, влажность, потраченную мощность обогревателем
  , TEMPERATURE // изменяем температуру выключения обогревателя
  , ERROR_VIEW // показываем ошибки
};
StateView state = MAIN_VIEW;

// ошибки
char error_msg[16 + 1]  = {0};

bool heater_state = false;
int required_temp = 18; // требуемая миимальная температура (берем из памяти)
float current_temp = 0.f; // температура на датчике
float display_temp = 0.f; // температура отображаемая на экране
float current_humidity = 0.f; // влажность на датчике
float display_humidity = 0.f; // влажность отображаемая на экране

const float DELTA_TEMP = 1.f; // чтобы постоянно не включался выключался обогревател будем сравнивать как t+delta | t-delta

unsigned long last_query_sensor = 0; // время последнего опроса датчик температуры+влажности

void DebugPrint(const char* str) {
  Serial.println(str);
}

void SetHeaterState(bool enable) {
  if (heater_state != enable) {
    heater_state = enable;
    digitalWrite(RELE_PIN, heater_state ? HIGH : LOW);
    DebugPrint(heater_state ? "heater enable" : "heater disable");
  }
}

// цикл логики опроса датчика и управлением реле
void Update()
{
  unsigned long t = millis();
  if (last_query_sensor > t) {
    last_query_sensor = 0; // защита от переполнения (переполнение раз в 50 дней)
  }
  // обновляем текущую температуру раз N секунд
  if (t - last_query_sensor > 1000) {
    last_query_sensor = t;
    current_temp = htu.readTemperature();
    current_humidity = htu.readHumidity();

    if (heater_state) {
      if (current_temp > (float)required_temp + DELTA_TEMP) {
        SetHeaterState(false);
      }
    } else {
      if (current_temp < (float)required_temp - DELTA_TEMP) {
        SetHeaterState(true);
      }
    }
  }
}

void PrintRequireTemperature() {
  char str[8] = {0};
  sprintf(str, "%i C", required_temp);
  lcd.setCursor((15 - strlen(str)) / 2, 1);
  lcd.print(str);
  DebugPrint(str);
}

// полностью перерисовывает экран
void ApplyStateDisplay() {
  lcd.clear();
  lcd.home();
  switch (state) {
    case MAIN_VIEW:
      lcd.print("temp:");
      lcd.setCursor(0, 1);
      lcd.print("humid:");
      display_temp = display_humidity = 0.f;
      break;
    case TEMPERATURE:
      lcd.print("change req.temp:");
      PrintRequireTemperature();
      break;
    case ERROR_VIEW:
      lcd.print(error_msg);
      break;
  }
}

void UpdateStateDisplay() {
  if (state == MAIN_VIEW) {
    char str[8];
    if (fabs(current_temp - display_temp) > 0.1f) {
      // обновляем температуру
      display_temp = current_temp;
      int v_int = (int)display_temp;
      int v_fra = (int) ((display_temp - (float)v_int) * 10);
      sprintf(str, "%d.%d C", v_int, v_fra);
      lcd.setCursor(15 - strlen(str), 0);
      lcd.print(str);
      DebugPrint(str);
    }
    if (fabs(current_humidity - display_humidity) > 0.1f) {
      // обновляем температуру
      display_humidity = current_humidity;
      int v_int = (int)display_humidity;
      int v_fra = (int) ((display_humidity - (float)v_int) * 10);
      sprintf(str, "%d.%d %%", v_int, v_fra);
      lcd.setCursor(15 - strlen(str), 1);
      lcd.print(str);
      DebugPrint(str);
    }
  }
}

void SelectBtnClick() {
  Serial.println("click select btn");
  if (state == MAIN_VIEW) {
    state = TEMPERATURE;
    ApplyStateDisplay();
  } else
  if (state == TEMPERATURE) {
    state = MAIN_VIEW;
    ApplyStateDisplay();
  }
}

void PlusBtnClick() {
  DebugPrint("click plus button");
  if (state == TEMPERATURE) {
    required_temp--;
    PrintRequireTemperature();
  }
}

void MinusBtnClick() {
  DebugPrint("click minus button");
  if (state == TEMPERATURE) {
    required_temp++;
    PrintRequireTemperature();
  }
}

void setup() {
  Serial.begin(9600);
  DebugPrint("start setup function");

  lcd.begin(16, 2);

  if (!htu.begin()) {
    DebugPrint("No temp. sensor!");
    strcpy(error_msg, "No temp. sensor!");
    state = ERROR_VIEW;
  } else {
    lcd.print("In setup func");
  }

  pinMode(RELE_PIN, OUTPUT);

  selectBtn.attachClick(&SelectBtnClick);
  plusBtn.attachClick(&PlusBtnClick);
  minusBtn.attachClick(&MinusBtnClick);
  DebugPrint("end setup function");

  ApplyStateDisplay();
}

void loop() {

  // обновляем кнопки
  selectBtn.tick();
  plusBtn.tick();
  minusBtn.tick();

  Update();
  UpdateStateDisplay();

  delay(128);
}
