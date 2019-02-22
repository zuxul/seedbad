
#define DEBUG

// желаемую температуру, дельту температуры, дельту времени, будем хранить в энергонезависимой памяти
#include <EEPROM.h>

// Термометр HTU21D
// Connect Vin to 3-5VDC (зеленый)
// Connect GND to ground (коричневый)
// Connect SCL to I2C clock pin (A5 on UNO) (синий)
// Connect SDA to I2C data pin (A4 on UNO) (оранжевый)

#include <Wire.h>
#include "SparkFunHTU21D.h"
HTU21D sensor;

// Дисплей LCD 1602
// LCD RS pin to digital pin 12
// LCD Enable pin to digital pin 11
// LCD D4 pin to digital pin 5
// LCD D5 pin to digital pin 4
// LCD D6 pin to digital pin 3
// LCD D7 pin to digital pin 2
// LCD R/W pin to ground
// 10K resistor:
// ends to +5V and ground
// wiper to LCD VO pin (pin 3)

#define _HAS_LCD_

#if defined(_HAS_LCD_)

#include <LiquidCrystal.h>

#else

struct LiquidCrystal {
  LiquidCrystal(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) {}
  void begin(uint8_t, uint8_t ) {};
  void setCursor(uint8_t, uint8_t) {};
  void print(const char*) {};
  void clear() {}
  void home() {};
};
#endif

LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// Кнопки
#define _HAS_BUTTONS_

#if defined(_HAS_BUTTONS_)
#include <OneButton.h>
#else
typedef void (*callbackFunction)(void);
struct OneButton {
  OneButton(uint8_t, bool) {};
  void attachClick(callbackFunction) {};
  void tick() {};
};
#endif

OneButton selectBtn(6, true);
OneButton plusBtn(7, true);
OneButton minusBtn(8, true);

// твердотельное реле
const unsigned int RELE_PIN = 9;

enum StateView {
  MAIN_VIEW = 0 // показываем температуру, влажность, потраченную мощность обогревателем
  , TEMP // изменяем температуру выключения обогревателя
  , DELTA_TEMP // изменяем delta_temp
  , DELTA_TIME // изменяем delta_time
};
StateView state = MAIN_VIEW;

// состояние обогревателя
bool heater_state = false;

byte required_temp = 0; // требуемая миимальная температура (берем из памяти, минимальная 1, максимум 30)
const byte max_temp = 30;
const byte min_temp = 1;
float delta_temp = 0.f; // чтобы постоянно не включался выключался обогреватель будем сравнивать как t+delta | t-delta
const float max_delta_temp = 2.5f; // ограничение - записываем в память как байт (умножаем на 10)
const float min_delta_temp = 0.1f;
float current_temp = 0.f; // температура на датчике
float display_temp = 0.f; // температура отображаемая на экране
float current_humidity = 0.f; // влажность на датчике
float display_humidity = 0.f; // влажность отображаемая на экране
unsigned long delta_time = 0; // чтобы постоянно не включался выключался обогреватель будем делать паузу между переключениями
const byte max_delta_time = 240; // 4 минуты
const byte min_delta_time = 2; // 2 секунды
unsigned long last_change_heater = 0; // время последнего переключения обогревателя
unsigned long last_query_sensor = 0; // время последнего опроса датчик температуры
unsigned long last_press_button = 0; // время последнего нажатия на кнопку (переходим в главное меню через 15 секунд простоя)


// таймер для сохранения переменных в eeprom (не делаем запись часто)
class DeltaTimer {
    const unsigned int pause = 5000;
    unsigned long timer;
  public:
    DeltaTimer(): timer(0) {}
    void start() {
      timer = millis() + pause;
    }
    bool check(unsigned long& t) {
      if (timer != 0 && timer < t) {
        timer = 0;
        return true;
      }
      return false;
    }
};

DeltaTimer temp_to_eeprom; // сохранение значение required_temp в память
DeltaTimer delta_temp_to_eeprom; // сохранение значение delta_temp в память
DeltaTimer delta_time_to_eeprom; // сохранение значение delta_time в память

void DebugPrint(const char* str) {
#ifdef DEBUG
  Serial.println(str);
#endif
}
void DebugPrint(int i) {
#ifdef DEBUG
  Serial.println(i);
#endif
}

void SetHeaterState(bool enable) {
  if (heater_state != enable) {
    heater_state = enable;
    DebugPrint(heater_state ? "heater enable" : "heater disable");
    delay(1000); // чтобы все другие потребители чуть отдохнули, т.к. приходят наводки и просадки
    digitalWrite(RELE_PIN, heater_state ? HIGH : LOW);
    delay(1000);
  }
}

// цикл логики опроса датчика и управлением реле
void Update()
{
  unsigned long t = millis();
  // защита от переполнения (переполнение раз в 50 дней)
  if (last_query_sensor > t) {
    last_query_sensor = 0;
  }
  if (last_change_heater > t) {
    last_change_heater = 0;
  }
  if (last_press_button > t) {
    last_press_button = 0;
  }
  // обновляем текущую температуру раз N секунд
  if (t - last_query_sensor > 2000) {
    last_query_sensor = t;

    float sensor_temp = sensor.readTemperature();
    if (sensor_temp < 100.f) { // если больше 100 - значит датчик "барохлит"
      current_temp = sensor_temp;
      current_humidity = sensor.readHumidity();
      if (current_humidity > 100.f) {
        current_humidity = 100.f;
      }
    }

    if (t - last_change_heater > delta_time)
    {
      if (heater_state) {
        if (current_temp > float(required_temp) + delta_temp) {
          last_change_heater = t;
          SetHeaterState(false);
        }
      } else {
        if (current_temp < float(required_temp) - delta_temp) {
          last_change_heater = t;
          SetHeaterState(true);
        }
      }
    }
  }

  // автоматический переход в главное меню после 15 сенкуд простоя
  if (last_press_button != 0 && t - last_press_button > 15000) {
    last_press_button = 0;
    if (state != MAIN_VIEW) {
      state = MAIN_VIEW;
      ApplyStateDisplay();
      DebugPrint("Go to main view by timeout");
    }
  }

  // сохранение переменных в память
  if (temp_to_eeprom.check(t)) {
    EEPROM.update(0, required_temp);
    DebugPrint("save required temperature");
  }
  if (delta_temp_to_eeprom.check(t)) {
    EEPROM.update(1, byte(delta_temp * 10.f));
    DebugPrint("save delta temperature");
  }
  if (delta_time_to_eeprom.check(t)) {
    EEPROM.update(2, byte(delta_time / 1000));
    DebugPrint("save delta time");
  }
}

void PrintRequireTemperature() {
  char str[16] = {0};
  const char* format = "   %i C   ";
  if (required_temp == min_temp) {
    format = "%i C (min)";
  } else if (required_temp == max_temp) {
    format = "%i C (max)";
  }
  sprintf(str, format, required_temp);
  lcd.setCursor((15 - strlen(str)) / 2, 1);
  lcd.print(str);
  DebugPrint(str);
}

void PrintDeltaTemperature() {
  char str[16] = {0};
  const char* format = "   %i.%i C   ";
  if (fabs(delta_temp - min_delta_temp) < 0.01f) {
    format = "%i.%i C (min)";
  } else if (fabs(delta_temp - max_delta_temp) < 0.01f) {
    format = "%i.%i C (max)";
  }
  int v_int = (int)delta_temp;
  int v_fra = (int) ((delta_temp - (float)v_int) * 10);
  sprintf(str, format, v_int, v_fra);
  lcd.setCursor((15 - strlen(str)) / 2, 1);
  lcd.print(str);
  DebugPrint(str);
}

void PrintDeltaTime() {
  char str[16] = {0};
  int s = delta_time / 1000;
  int m = s / 60;
  s -= (m * 60);
  const char* f = "%01im:%02is";
  sprintf(str, f, m, s);
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
    case TEMP:
      lcd.print("change req.temp");
      PrintRequireTemperature();
      break;
    case DELTA_TEMP:
      lcd.print("change delt.temp");
      PrintDeltaTemperature();
      break;
    case DELTA_TIME:
      lcd.print("change delt.time");
      PrintDeltaTime();
      break;
  }
}

void UpdateStateDisplay() {
  if (state == MAIN_VIEW) {
    char str[8];
    if (fabs(current_temp - display_temp) > 0.01f) {
      // обновляем температуру
      display_temp = current_temp;
      int v_int = (int)display_temp;
      int v_fra = (int) ((display_temp - (float)v_int) * 100);
      sprintf(str, " %d.%02d C", v_int, v_fra);
      lcd.setCursor(15 - strlen(str), 0);
      lcd.print(str);
      DebugPrint(str);
    }
    if (fabs(current_humidity - display_humidity) > 0.1f) {
      // обновляем влажность
      display_humidity = current_humidity;
      int v_int = (int)display_humidity;
      int v_fra = (int) ((display_humidity - (float)v_int) * 100);
      sprintf(str, " %d.%02d %%", v_int, v_fra);
      lcd.setCursor(15 - strlen(str), 1);
      lcd.print(str);
      //DebugPrint(str);
    }
  }
}

void SelectBtnClick() {
  DebugPrint("click select btn");
  last_press_button = millis();
  if (state == MAIN_VIEW) state = TEMP;
  else if (state == TEMP) state = DELTA_TEMP;
  else if (state == DELTA_TEMP) state = DELTA_TIME;
  else if (state == DELTA_TIME) state = MAIN_VIEW;
  ApplyStateDisplay();
}

void PlusBtnClick() {
  DebugPrint("click plus button");
  last_press_button = millis();
  switch (state) {
    case TEMP:
      if (required_temp < max_temp) {
        required_temp++;
        PrintRequireTemperature();
      }
      temp_to_eeprom.start();
      break;
    case DELTA_TEMP:
      if (delta_temp < max_delta_temp) {
        delta_temp += 0.1f;
        if (delta_temp > max_delta_temp) {
          delta_temp = max_delta_temp;
        }
        PrintDeltaTemperature();
      }
      delta_temp_to_eeprom.start();
      break;
    case DELTA_TIME:
      int dt = int(delta_time / 1000);
      if (dt < 10) dt++;
      else if (dt < 60) dt += 10;
      else if (dt < max_delta_time) dt += 30;
      if (dt > max_delta_time) dt = max_delta_time;
      delta_time = 1000 * (unsigned long)dt;
      PrintDeltaTime();
      delta_time_to_eeprom.start();
      break;
  }
}

void MinusBtnClick() {
  DebugPrint("click minus button");
  last_press_button = millis();
  switch (state) {
    case TEMP:
      if (required_temp > min_temp) {
        required_temp--;
        PrintRequireTemperature();
      }
      temp_to_eeprom.start();
      break;
    case DELTA_TEMP:
      if (delta_temp > min_delta_temp) {
        delta_temp -= 0.1f;
        if (delta_temp < min_delta_temp) {
          delta_temp = min_delta_temp;
        }
        PrintDeltaTemperature();
      }
      delta_temp_to_eeprom.start();
      break;
    case DELTA_TIME:
      int dt = int(delta_time / 1000);
      if (dt > 60) dt -= 30;
      else if (dt > 10) dt -= 10;
      else if (dt > min_delta_time) dt--;
      if (dt < min_delta_time) dt = min_delta_time;
      delta_time = 1000 * (unsigned long)dt;
      PrintDeltaTime();
      delta_time_to_eeprom.start();
      break;
  }
}

void setup() {
#ifdef DEBUG
  Serial.begin(9600);
#endif
  DebugPrint("start setup function");

  required_temp = EEPROM.read(0);
  if (required_temp < min_temp) {
    required_temp = min_temp;
  } else if (required_temp > max_temp) {
    required_temp = max_temp;
  }

  delta_temp = 0.1f * EEPROM.read(1);
  if (delta_temp < min_delta_temp) {
    delta_temp = min_delta_temp;
  } else if (delta_temp > max_delta_temp) {
    delta_temp = max_delta_temp;
  }

  byte dt = EEPROM.read(2);
  if (dt < min_delta_time) {
    dt = min_delta_time;
  } else if (dt > max_delta_time) {
    dt = max_delta_time;
  }
  delta_time = 1000 * (unsigned long)dt;

#ifdef DEBUG
  Serial.print("Required temp:");
  Serial.println(required_temp);
#endif

  lcd.begin(16, 2);

  sensor.begin();

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
