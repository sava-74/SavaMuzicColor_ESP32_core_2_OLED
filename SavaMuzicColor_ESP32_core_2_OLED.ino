/*
//***********************************************************************
// SavaMuzicColor_ESP32_core_2_OLED.ino
// Версия 2.0.0
// Автор: SavaLab
// https://github.com/sava-74/SavaMuzicColor_ESP32_core_2_OLED.git
//***********************************************************************
*/
#include "SavaOLED_ESP32.h"                                // библиотека дисплея OLED
#include "SavaGFX_OLED.h"                                  // библиотека графики для OLED
#include "Fonts/SF_Font_P8.h"                              // шрифт 8px
#include "Fonts/SF_Font_x2_P16.h"                          // шрифт 16px
#include "esp_dsp.h"                                       // Подключаем основную библиотеку для цифровой обработки сигналов на ESP32
#include <math.h>                                          // Подключаем математическую библиотеку для использования константы M_PI (число Пи)
#include "esp_timer.h"                                     // Подключаем библиотеку для работы с высокоточным таймером ESP32
#include <SavaTrig.h>                                      // Библиотека тригеров
#include <SavaTime.h>                                      // Библиотека таймеров
#include <SavaButton.h>                                    // Библиотека кнопок
#include <EEPROM.h>                                        // библиотека памяти
#include <SavaLED_ESP32.h>                                 // библиотека управления светодиодной лентой
SavaOLED_ESP32 oled(128, 64);                              // объявляем дисплей
SavaGFX_OLED gfx(&oled);                                   // объявляем графику
#define SAMPLES                 256                        // Количество семплов (уменьшено для скорости)
#define SAMPLING_FREQ           24400                      // Частота дискритизации
#define AUDIO_IN_PIN            39                         // АЦП пин
#define LED_PIN                 14                         // WS2912 пин
#define NUM_BANDS               8                          // количество каналов
#define EEPROM_SIZE             24                         // размер памяти
#define EEPROM_MAGIC_KEY        0x1A2E                     // секретный ключ востановления по дефолту памяти                          
#define NUM_LEDS                100                        // кол-во светодиодов в ленте
#define BACKGROUND_BRIGHTNESS   40                         // яркость фона
#define TIMERAUTOCYCLE          3000                       // таймер переключения эффектов в мСек
#define IDLE_TIMEOUT            20000                      // Время отсутствия звука в мСек
#define ENABLE_GATE_FILTER                                 // Включить Gate фильтр (закомментировать для версии без микрофона)
#define GATE_TON_MS             500                        // Задержка включения канала (фильтр импульсов)
#define GATE_TOF_MS             350                        // Задержка выключения канала (удержание)
#define SPEECH_DETECT_TIMEOUT   1000                       // Задержка детекции речи (мс)
#define SPEECH_HF_THRESHOLD     50                         // Порог высоких каналов (5-7), речь < 50
#define SPEECH_LF_MIN           120                         // Минимальная активность низких каналов (0-4)
SavaLED_ESP32 strip;
//****************************************************************************************
#define BTN_PLUS_PIN    5                                     // кнопка плюс
#define BTN_MINUS_PIN   13                                    // кнопка минус
#define BTN_OK_PIN      15                                    // кнопка ок
const int NUM_EFFECTS = 6;                                    // Количество визуальных эффектов (0-5)
const int NUM_BG_OPTIONS = 5;                                 // Количество опций фона (0-8)
const int NUM_MENU_ITEMS = 5;                                 // колво пунктов меню
SavaButton btn_minus;                                         // кнопка минус
SavaButton btn_ok;                                            // кнопка ок
SavaButton btn_plus;                                          // кнопка плюс
SavaTrig trigRT_minus;                                        // триггер переднего фронта для минус
SavaTrig trigRT_ok;                                           // триггер переднего фронта для OK
SavaTrig trigRT_plus;                                         // триггер переднего фронта для плюс
//****************************************************************************************
// --- Настройки визуализации OLED ---
// Диапазон для настройки плавности (FADE_SPEED).
const float SMOOTH_MIN_SPEED = 4.0f;                          // Максимальная скорость падения (при 0% плавности).
const float SMOOTH_MAX_SPEED = 0.5f;                          // Минимальная скорость падения (при 100% плавности).
const float PEAK_FADE_SPEED = 0.2f;                           // Скорость падения пиковых индикаторов. Чем БОЛЬШЕ значение, тем БЫСТРЕЕ падают.
const float FADE_SPEED = 2.0f;                                // <<< ВОЗВРАЩАЕМ КОНСТАНТУ. Чем БОЛЬШЕ, тем БЫСТРЕЕ падают.
int displayBarHeights[NUM_BANDS] = {0};                       // Массив для хранения текущей "видимой" высоты столбиков (для плавного затухания)
int peakBarHeights[NUM_BANDS] = {0};                          // Массив для хранения высоты пиковых индикаторов
// --- Переменные для управления экранами OLED ---
SavaTime menuTimeoutTimer;                                    // Создаем экземпляр таймера
SavaTrig menuExitTrigger;                                     // Триггер для отслеживания момента выхода из меню
SavaTrig channel_triggers[NUM_BANDS];
const uint32_t MENU_TIMEOUT = 5000;                           // 5 секунд бездействия окне меню
bool FlagIDLE = false;                                        // Флаг бездействия
SavaTime FPS_Timer;                                           // Таймер для контроля FPS
// band_gain_factors НЕ ИСПОЛЬЗУЮТСЯ в новом коде (можем добавить потом если нужно)
//***************************************************************************************
const uint32_t channel_colors[NUM_BANDS] = {
  //RED, ORANGE, YELLOW, BLUE, CYAN, SKYBLUE, LIME, GREEN     // Цвета для каналов
  RED,GREEN,WHITE,YELLOW,BLUE,ORANGE,LIME,CYAN
  };
const uint32_t backgr_colors[NUM_BG_OPTIONS] = {
  LIME, RED, BLUE, WHITE, BLACK                               // 4 цвета + "Выкл"
  };
const uint8_t backgr_colors_rgb[NUM_BG_OPTIONS][3] = {
  {  0, 255,   0}, // 0: LIME
  {220,  20,  60}, // 1: CRIMSON
  {  0, 255, 255}, // 2: CYAN
  {255, 255, 255}, // 3: WHITE
  {  0,   0,   0}  // 4: BLACK (Выкл)
};
SavaTime idleTimer;                                           // создание таймера бездействия
//****************************************************************************************
//****************************************************************************************
SavaTime autoCycleTimer;                                      //создание тамера переключения эффектов в режиме авто
//****************************************************************************************
QueueHandle_t peaksQueue;                                     // Наша очередь для передачи данных
// --- Переменные, используемые только на ядре 0 ---
esp_timer_handle_t sampling_timer;                            // Таймер для сбора семплов  
portMUX_TYPE isrMux = portMUX_INITIALIZER_UNLOCKED;           // portMUX для ISR, он легковеснее
volatile uint16_t raw_samples[SAMPLES];                       // Буфер для сырых семплов
volatile uint16_t sample_index = 0;                           // Текущий индекс в буфере raw_samples
volatile bool samplesReadyFlag = false;                       // Флаг готовности данных
//****************************************************************************************
// Разрешение FFT: 24400 / 256 = 95.3 Гц/бин
// Канал 1: 150-259 Гц   → бины 2-3   (150/95.3=1.57, 259/95.3=2.72)
// Канал 2: 260-449 Гц   → бины 3-5   (260/95.3=2.73, 449/95.3=4.71)
// Канал 3: 450-779 Гц   → бины 5-8   (450/95.3=4.72, 779/95.3=8.17)
// Канал 4: 780-1349 Гц  → бины 9-14  (780/95.3=8.18, 1349/95.3=14.15)
// Канал 5: 1350-2330 Гц → бины 15-24 (1350/95.3=14.16, 2330/95.3=24.45)
// Канал 6: 2340-4040 Гц → бины 25-42 (2340/95.3=24.55, 4040/95.3=42.39)
// Канал 7: 4050-7000 Гц → бины 43-73 (4050/95.3=42.50, 7000/95.3=73.45)
// Канал 8: 7010-12200 Гц→ бины 74-127(7010/95.3=73.55, 12200/95.3=128.01, макс 127)
const int band_max_bin[8] = {3, 5, 8, 14, 24, 42, 73, 127};  // Верхние границы для 256 семплов @ 24400 Гц
//****************************************************************************************
// --- Настройки обработки FFT в ядре 0 ---
//****************************************************************************************
// Порог детекции атаки (резкий скачок уровня для детектора isNewPeak)
// 25 = ~10% от максимума (255), оптимально для фильтрации шума
#define ATTACK_THRESHOLD 25
// =========================================================================
// --- Структура для передачи обработанных данных между ядрами ---
// =========================================================================
struct BandData {                                             // Структура для передачи данных по каналам
  uint8_t level;                                              // Нормализованный уровень 0-255
  bool isNewPeak;                                             // Флаг резкой атаки (для триггеров эффектов)
};
//*****************************************************************************************
struct Settings {                                             // Структура для хранения настроек
  uint8_t brightness, currentEffect, sensitivity, backgroundColor;  // яркость (0-100), эффект (0-5), чувствительность (10-100), цвет фона (0-4)
  uint16_t numLeds;                                           // количество светодиодов (60-600)      
  uint8_t smooth;                                             // плавность (0-100)
  uint16_t magic_key;                                         // "магический ключ" для проверки целостности данных  
  };
Settings currentSettings;                                     // Текущие настройки

//*****************************************************************************************
// Режимы меню
enum MenuMode {
  MENU_MODE_EFFECT,                                           // Режим выбора эффекта (по умолчанию)
  MENU_MODE_SETTINGS                                          // Режим настроек (вход по длинному OK)
};
MenuMode currentMenuMode = MENU_MODE_EFFECT;                  // Текущий режим меню

// Пункты меню настроек (БЕЗ выбора эффекта!)
enum MenuItem {
  MENU_BRIGHTNESS,                                            // Яркость
  MENU_SENSITIVITY,                                           // Чувствительность           
  MENU_BACKGROUND,                                            // Фон         
  MENU_SMOOTH                                                 // Плавность           
};
MenuItem currentMenuItem = MENU_BRIGHTNESS;                   // Текущий пункт меню
//****************************************************************************************
// --- Структура и массив для эффекта "Танцы плюс" ("Искры") ---
//****************************************************************************************
#define N_SPARKS 30                                           // Максимальное количество "искр" на экране

struct Spark {
  bool active;                                                // Активна ли искра?
  float speed;                                                // Индивидуальная скорость "искры"
  int age;                                                    // Возраст в кадрах
  uint32_t color;                                             // Цвет искры
};

Spark sparks[N_SPARKS];                                       // Глобальный массив-"инкубатор" для искр

// Базовая скорость "искры" в пикселях за кадр, в зависимости от канала (частоты)
const float base_speed_by_channel[NUM_BANDS] = {
  0.08f,  // Канал 0 (бас) - медленные
  0.10f,
  0.12f,
  0.16f,  // Канал 3 (середина)
  0.18f,
  0.20f,
  0.22f,
  0.24f   // Канал 7 (верх) - быстрые
};
//****************************************************************************************
// --- Структура и массив для эффекта "Звезды" ---
//****************************************************************************************
#define MAX_STARS 40                                          // Максимальное количество "звезд" на экране

struct Star {
  bool active;                                                // Активна ли звезда?
  int position;                                               // Позиция на ленте
  int brightness;                                             // Текущая яркость (0-255)
  uint32_t color;                                             // Цвет звезды
};

Star starPool[MAX_STARS]; // Глобальный массив-"пул" для звезд
//****************************************************************************************
//--- прототипы функций ---
//****************************************************************************************
void Vizual_OLED(BandData* band_data_copy);                 // отрисовка анализатора на OLED
void Menu_OLED();                                           // отрисовка меню на OLED      
void IDLE_OLED();                                           // отрисовка экрана IDLE на OLED
bool buttonsH();                                            // обработка кнопок
void saveSettings();                                        // сохранение настроек в EEPROM
void loadSettings();                                        // загрузка настроек из EEPROM
void ColorMuzik(BandData* band_data);                       // эффект ЦветМузыка
void effect_Stroboscope(BandData* band_data);               // эффект Стробоскоп
void effect_VuMeter_Gradient(BandData* band_data);          // эффект VU-Метр Градиент        
void spawnSparks(BandData* band_data);                      // спавн искр для эффекта Танцы+
void effect_DanceParty();                                   // эффект Танцы+
void spawnStars(BandData* band_data);                       // спавн звезд для эффекта Звезды
void effect_Stars();                                        // эффект Звезды
//*****************************************************************************************
// --- Функция отрисовки анализатора на OLED дисплее ---
//*****************************************************************************************
void Vizual_OLED(BandData* band_data_copy) {                // Экран анализатора

    // --- Подготовка массива уровней для SavaGFX эквалайзера ---
    static uint8_t levels[NUM_BANDS] = {0};                 // Массив уровней для эквалайзера

    for (int i = 0; i < NUM_BANDS; i++) {
        levels[i] = band_data_copy[i].level;                // Копируем уровни (0-255)
    }
    // --- Отрисовка с контролем FPS ---
    //if (FPS_Timer.Gen(25)) {                                // Ограничение до ~25 FPS
        oled.clear();                                       // Очищаем экран
        // Используем готовый эквалайзер из SavaGFX_OLED
        // peaks=true, peakDecaySpeed=10 (100ms на уровень)
        gfx.equalizer8(levels, true, 10);                   // Отрисовка эквалайзера с пиковыми индикаторами
        //oled.display();                                     // Обновляем дисплей
    //}
}
//*****************************************************************************************
// --- Функция отрисовки меню на OLED дисплее (2 режима) ---
//*****************************************************************************************
void Menu_OLED() {                                          // Экран меню      
    //oled.clear();                                         // Очищаем экран
    oled.font(SF_Font_x2_P16);                            // Крупный шрифт 16px

    // === РЕЖИМ 1: ВЫБОР ЭФФЕКТА ===
    if (currentMenuMode == MENU_MODE_EFFECT) {            // Режим выбора эффекта
        oled.cursor(0, 8, StrCenter);                     // Позиция для текста      
        oled.print("Эффект");                             // Заголовок        
        oled.drawPrint();                                 // ОБЯЗАТЕЛЬНО после print() 

        oled.cursor(0, 40, StrCenter);                    // Позиция для текста эффекта
        const char* effectNames[] = { "ЦветМузыка", "Стробоскоп", "VU-Метр", "Искры+", "Звезды", "Авто" };
        oled.print(effectNames[currentSettings.currentEffect]); // Название эффекта
        oled.drawPrint();                                 // ОБЯЗАТЕЛЬНО после print()
    }

    // === РЕЖИМ 2: НАСТРОЙКИ ===
    else if (currentMenuMode == MENU_MODE_SETTINGS) {
        oled.cursor(0, 8, StrCenter);

        switch (currentMenuItem) {
            case MENU_BRIGHTNESS:   oled.print("Яркость");    break;
            case MENU_SENSITIVITY:  oled.print("Чувств.");    break;
            case MENU_BACKGROUND:   oled.print("Фон");        break;
            case MENU_SMOOTH:       oled.print("Плавность");  break;
        }
        oled.drawPrint();

        oled.cursor(0, 40, StrCenter);

        switch (currentMenuItem) {
            case MENU_BRIGHTNESS:
                oled.print(currentSettings.brightness);
                oled.print("%");
                break;
            case MENU_SENSITIVITY:
                oled.print(currentSettings.sensitivity);
                oled.print("%");
                break;
            case MENU_BACKGROUND: {
                if (currentSettings.backgroundColor < 4) {
                    const char* backgroundNames[] = { "Зеленый", "Красный", "Синий", "Белый" };
                    oled.print(backgroundNames[currentSettings.backgroundColor]);
                } else {
                    oled.print("Выкл.");
                }
                break;
            }
            case MENU_SMOOTH:
                oled.print(currentSettings.smooth);
                oled.print("%");
                break;
        }
        oled.drawPrint();
    }

    //oled.display();
}
//*****************************************************************************************
// --- Функция отрисовки меню на OLED дисплее ---
//*****************************************************************************************
void IDLE_OLED(){                                           // Экран ТИШИНА
    //oled.clear();
    oled.font(SF_Font_x2_P16);                        // Крупный шрифт 16px
    oled.cursor(0, 22, StrCenter);                    // Позиция для текста
    oled.print("ТИШИНА");
    oled.drawPrint();                                 // ОБЯЗАТЕЛЬНО после print()
    //oled.display();                                   // Обновляем дисплей  
}
//*****************************************************************************************
// --- Управление кнопками (2 режима меню) ---
//*****************************************************************************************
bool buttonsH(){

  // Читаем кнопки
  bool butMinus = trigRT_minus.RT(btn_minus.read());        // Минус → уменьшение значения
  bool butPlus = trigRT_plus.RT(btn_plus.read());           // Плюс → увеличение значения

  // Для OK используем readLong(1000мс) чтобы различать короткое/длинное
  uint8_t okEvent = btn_ok.readLong();                      // OK → короткое/длинное нажатие
  bool butOkShort = (okEvent == BTN_CLICK);                 // Короткое нажатие
  bool butOkLong = (okEvent == BTN_LONG);                   // Длинное нажатие

  bool anyButton = butMinus || butPlus || butOkShort || butOkLong;  // Любая кнопка нажата

  // Обрабатываем логику только если меню активно!
  if (menuTimeoutTimer.TOF(MENU_TIMEOUT, anyButton)) {

    // === РЕЖИМ 1: ВЫБОР ЭФФЕКТА ===
    if (currentMenuMode == MENU_MODE_EFFECT) {

      // Плюс → следующий эффект (СРАЗУ переключается!)
      if (butPlus) {
        currentSettings.currentEffect = (currentSettings.currentEffect + 1) % NUM_EFFECTS;
      }

      // Минус → предыдущий эффект (СРАЗУ переключается!)
      if (butMinus) {
        if (currentSettings.currentEffect > 0) currentSettings.currentEffect--;
        else currentSettings.currentEffect = NUM_EFFECTS - 1;
      }

      // OK короткое → ничего не делаем (просто показываем меню)

      // OK длинное → переход в режим НАСТРОЕК
      if (butOkLong) {
        currentMenuMode = MENU_MODE_SETTINGS;
        currentMenuItem = MENU_BRIGHTNESS; // Начинаем с яркости
      }
    }

    // === РЕЖИМ 2: НАСТРОЙКИ ===
    else if (currentMenuMode == MENU_MODE_SETTINGS) {

      // OK короткое → следующий пункт настроек
      if (butOkShort) {
        currentMenuItem = (MenuItem)((currentMenuItem + 1) % 4); // 4 пункта настроек
      }

      // Плюс → увеличить значение
      if (butPlus) {
        switch (currentMenuItem) {
          case MENU_BRIGHTNESS:
            if (currentSettings.brightness < 95) currentSettings.brightness += 5;
            else currentSettings.brightness = 100;
            break;
          case MENU_SENSITIVITY:
            if (currentSettings.sensitivity < 100) currentSettings.sensitivity += 10;
            break;
          case MENU_BACKGROUND:
            currentSettings.backgroundColor = (currentSettings.backgroundColor + 1) % NUM_BG_OPTIONS;
            break;
          case MENU_SMOOTH:
            if (currentSettings.smooth < 100) currentSettings.smooth += 10;
            break;
        }
      }

      // Минус → уменьшить значение
      if (butMinus) {
        switch (currentMenuItem) {
          case MENU_BRIGHTNESS:
            if (currentSettings.brightness > 55) currentSettings.brightness -= 5;
            else currentSettings.brightness = 50;
            break;
          case MENU_SENSITIVITY:
            if (currentSettings.sensitivity > 10) currentSettings.sensitivity -= 10;
            break;
          case MENU_BACKGROUND:
            if (currentSettings.backgroundColor > 0) currentSettings.backgroundColor--;
            else currentSettings.backgroundColor = NUM_BG_OPTIONS - 1;
            break;
          case MENU_SMOOTH:
            if (currentSettings.smooth > 0) currentSettings.smooth -= 10;
            break;
        }
      }
    }

    // Применяем яркость
    strip.setBrightness(map(currentSettings.brightness, 50, 100, 127, 255));
  }

  return anyButton;  // Возвращаем флаг нажатия любой кнопки
}
//*****************************************************************************************
// --- Функции записи в EEPROM ---
//*****************************************************************************************
void saveSettings() {
  // Перед сохранением устанавливаем "магический ключ", чтобы подтвердить целостность данных
  currentSettings.magic_key = EEPROM_MAGIC_KEY;
  // Записываем всю структуру currentSettings в EEPROM, начиная с адреса 0
  EEPROM.put(0, currentSettings);
  // Подтверждаем запись. Это важно для ESP32!
  EEPROM.commit();
}
//*****************************************************************************************
// --- Функции загрузки из EEPROM---
//*****************************************************************************************
void loadSettings() {
  // Создаем временную структуру для чтения
  Settings storedSettings;
  // Читаем данные из EEPROM в эту структуру
  EEPROM.get(0, storedSettings);

  // Проверяем "магический ключ"
  if (storedSettings.magic_key == EEPROM_MAGIC_KEY) {
    // Ключ совпал - данные корректны. Копируем их в нашу рабочую переменную.
    currentSettings = storedSettings;
    
    // Дополнительная проверка на адекватность значений (защита от сбоев)
    if (currentSettings.brightness < 5) currentSettings.brightness = 5;
    if (currentSettings.currentEffect >= NUM_EFFECTS) currentSettings.currentEffect = 0;
    if (currentSettings.sensitivity < 10 || currentSettings.sensitivity > 100) currentSettings.sensitivity = 40;
    if (currentSettings.backgroundColor >= NUM_BG_OPTIONS) currentSettings.backgroundColor = NUM_BG_OPTIONS - 1;
    if (currentSettings.numLeds < 60 || currentSettings.numLeds > 600) currentSettings.numLeds = 60;
    if (currentSettings.smooth > 100) currentSettings.smooth = 30;

  } else {
    // Ключ не совпал - EEPROM пуста или данные повреждены.
    // Загружаем настройки по умолчанию.
    currentSettings.brightness = 128;
    currentSettings.currentEffect = 0;
    currentSettings.sensitivity = 40;
    currentSettings.backgroundColor = NUM_BG_OPTIONS - 1;
    currentSettings.numLeds = 60;
    currentSettings.smooth = 30;
    
    // И сразу же сохраняем их в EEPROM для будущих запусков
    saveSettings();
  }
  // Обновляем глобальную переменную NUM_LEDS значением из настроек
  // NUM_LEDS = currentSettings.numLeds; // Пока закомментируем, т.к. лента не подключена
}
//****************************************************************************************
// --- Callback-функция для esp_timer ---
//****************************************************************************************
void IRAM_ATTR sampling_timer_callback(void* arg) {
    portENTER_CRITICAL_ISR(&isrMux);
    if (!samplesReadyFlag) { // Защита от перезаписи, пока данные не скопированы
        raw_samples[sample_index] = analogRead(AUDIO_IN_PIN);
        sample_index = sample_index+1;
        if (sample_index >= SAMPLES) {
            sample_index = 0;
            samplesReadyFlag = true;
        }
    }
    portEXIT_CRITICAL_ISR(&isrMux);
}
//****************************************************************************************
// --- Функция Setup для настроек ---
//****************************************************************************************
void setup() {
  // --- Инициализация OLED (I2C автоматически настраивается) ---
  oled.init(800000, 21, 22);                      // 800kHz, SDA=21, SCL=22 (стандарт ESP32)
  oled.clear();                                   // Очищаем экран
  
  oled.font(SF_Font_P8);
  oled.cursor(0, 0, StrLeft);
  // --- Инициализация кнопок через SavaButton ---
  btn_minus(BTN_MINUS_PIN, PLUS);                 // GPIO13, подтяжка к плюсу
  btn_ok(BTN_OK_PIN, PLUS);                       // GPIO15, подтяжка к плюсу
  btn_plus(BTN_PLUS_PIN, PLUS);                   // GPIO5, подтяжка к плюсу
  btn_ok.setLong(1000);                           // Длинное нажатие 1000 мс
  oled.print("Настройка кнопок...");              // Текст заставки
  oled.drawPrint();                               // Обновляем дисплей
  oled.display();                                 // Отправляем на экран
  delay(1000);

    // --- Инициализация LED ленты ---
  oled.cursor(0, 9, StrLeft);                    // Позиция для текста
  strip.begin(NUM_LEDS, LED_PIN);                 // Инициализация ленты
  oled.print("Подключаем LED ленту...");          // Текст заставки
  strip.setGammaCorrection(true);                 // Включаем гамма-коррекцию
  strip.clear();                                  //  
  oled.drawPrint();
  oled.display();
  delay(1000);

  // --- ИНИЦИАЛИЗАЦИЯ АЦП ---
  analogReadResolution(12);                       // Разрешение АЦП 12 бит  
  analogSetAttenuation(ADC_11db);                 // Аттенюатор для полного диапазона 0-3.3В
  oled.cursor(0, 18, StrLeft);                    // Позиция для текста
  oled.print("Настройка АЦП...");                 // Текст заставки
  oled.drawPrint();                               // Обновляем дисплей
  oled.display();                                 // Отправляем на экран
  delay(1000);
  
  Serial.begin(115200);                           // Инициализация Serial

  // --- ИНИЦИАЛИЗАЦИЯ И ЗАГРУЗКА НАСТРОЕК ---
  EEPROM.begin(EEPROM_SIZE);                      // Инициализация EEPROM
  oled.cursor(0, 27, StrLeft);                    // Позиция для текста            
  oled.print("Подключение памяти...");               // Текст заставки
  oled.drawPrint();                               // Обновляем дисплей     
  oled.display();                                 // Отправляем на экран
  delay(1000);

  loadSettings();                                 // Загрузка настроек из EEPROM
  Serial.print("Sensitivity: "); Serial.print(currentSettings.sensitivity); Serial.println("%");  // Вывод в Serial для отладки
  oled.cursor(0, 36, StrLeft);                    // Позиция для текста 
  oled.print("Загрузка настроек...");                // Текст заставки
  oled.drawPrint();                               // Обновляем дисплей
  oled.display();                                 // Отправляем на экран
  delay(1500);   
  
  // --- Инициализация библиотеки esp-dsp ---
  // Выполняется один раз для подготовки внутренних таблиц, что ускоряет FFT.
  esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);      // Инициализация FFT
  if (ret != ESP_OK) {
    Serial.println("FFT initialization failed!");
    return;
  }
  oled.cursor(0, 45, StrLeft);
  oled.print("Подключаем Фурье(FFT)...");                             // Текст заставки
  oled.drawPrint();
  oled.display();
  delay(1500);
  // --- ИНИЦИАЛИЗАЦИЯ ОЧЕРЕДИ ---
    peaksQueue = xQueueCreate(1, sizeof(BandData) * NUM_BANDS);             // очередь для передачи данных между ядрами

    // Создаем и запускаем задачу для FFT на ядре 0
    xTaskCreatePinnedToCore(                                                // Создание задачи
        TaskFFTcode, "TaskFFT", 32768, NULL, 10, NULL, 0);                  // Запуск на ядре 0
  
  Serial.println("Setup complete.");
  oled.cursor(0, 54, StrCenter);
  oled.print("Загрузка завершина!");                             // Текст заставки
  oled.drawPrint();
  oled.display();
  delay(1500);
    // --- Заставка SAVA ---
  oled.clear(); 
  oled.rectR(0, 0, 127, 63, 3, REPLACE); // Прямоугольник со скруглением r=3
  oled.font(SF_Font_x2_P16);                      // Крупный шрифт (16px недостаточно для scale(4), но покажем)
  oled.cursor(0, 22, StrCenter);                  // Позиция для текста
  oled.drawMode(ADD_UP);                          // Режим наложения
  oled.print("SavaLab");                             // Текст заставки
  oled.drawPrint();
  oled.font(SF_Font_P8);
  oled.cursor(0, 39, StrCenter);
  oled.print("Эксклюзивно");
  oled.drawPrint();
  oled.cursor(0, 48, StrCenter);
  oled.print("для А.С.Бердюгина");
  oled.drawPrint();
  oled.display();
  delay(2000);
  oled.clear();                                                             // Очищаем экран
  //disableCore0WDT();
  //disableCore1WDT();
}
//****************************************************************************************
// --- ЯДРО 0: FFT обработка звука (ПОЛНОСТЬЮ ПЕРЕПИСАНО) ---
//****************************************************************************************
void TaskFFTcode(void * pvParameters) {
    Serial.print("TaskFFT running on core ");
    Serial.println(xPortGetCoreID());

    // === БУФЕРЫ ДЛЯ FFT (статические, создаются один раз) ===
    static float fft_input[SAMPLES];           // Входные данные для FFT
    static float temp_fft_buffer[SAMPLES * 2]; // Комплексный буфер [Re, Im, Re, Im...]
    static float hamming_window[SAMPLES];      // Окно Хэмминга (генерируется один раз)

    // === ГЕНЕРАЦИЯ ОКНА ХЭММИНГА (один раз при старте) ===
    for (int i = 0; i < SAMPLES; i++) {
        hamming_window[i] = 0.54f - 0.46f * cosf(2 * M_PI * i / (SAMPLES - 1));
    }

    // === ДЕТЕКТОРЫ АТАКИ ===
    static uint8_t last_level[NUM_BANDS] = {0};

    // === GATE ФИЛЬТР (TON/TOF для каждого канала) ===
    #ifdef ENABLE_GATE_FILTER
    static SavaTime gateTimerTON[NUM_BANDS];
    static SavaTime gateTimerTOF[NUM_BANDS];
    static bool channelGateActive[NUM_BANDS] = {false};
    #endif

    // === ДЕТЕКТОР РЕЧИ ===
    static SavaTime speechTimer;

    // === МАССИВ ДЛЯ ОТПРАВКИ В ОЧЕРЕДЬ ===
    BandData band_data[NUM_BANDS];

    // === НАСТРОЙКА ESP_TIMER ДЛЯ СБОРА СЕМПЛОВ ===
    const esp_timer_create_args_t timer_args = {
        .callback = &sampling_timer_callback,
        .name = "audio_sampler"
    };
    esp_timer_create(&timer_args, &sampling_timer);                       // Создаем таймер
    esp_timer_start_periodic(sampling_timer, 1000000 / SAMPLING_FREQ);    // Запускаем таймер с нужной периодичностью

    // === КОНСТАНТЫ FFT ===
    const int start_bin_freq = 2;                                         // 150 Гц / 95.3 Гц/бин ≈ 1.57 → 2
    const float MIN_AMPLITUDE[] = {
                                  6000.0f, //8600.0f, 
                                  6000.0f, //8100.0f, 
                                  2000.0f, //5000.0f, 
                                  2200.0f, //5000.0f,
                                  2000.0f, //2000.0f,
                                  1000.0f, //8000.0f,
                                  2000.0f,
                                  600.0f};                                   
    const float MAX_AMPLITUDE[] = {
                                  45000.0f, 
                                  40000.0f, 
                                  40000.0f, 
                                  40000.0f,
                                  30000.0f,
                                  40000.0f,
                                  40000.0f,
                                  15000.0f};                               

    // === ГЛАВНЫЙ ЦИКЛ ===
    for (;;) {
        if (samplesReadyFlag) {

            // ============================================================
            // ШАГ 1: КОПИРОВАНИЕ И ПОДГОТОВКА ДАННЫХ
            // ============================================================
            portENTER_CRITICAL(&isrMux);                                  // Защита от прерываний  
            for (int i = 0; i < SAMPLES; i++) {                           // Копируем сырые семплы в float буфер
                fft_input[i] = (float)raw_samples[i];                     // Преобразование в float
            }
            samplesReadyFlag = false;                                     // Сбрасываем флаг готовности 
            portEXIT_CRITICAL(&isrMux);                                   // Снимаем защиту от прерываний

            // ============================================================
            // ШАГ 2: УДАЛЕНИЕ DC OFFSET (постоянная составляющая)
            // ============================================================
            float mean = 0;
            for (int i = 0; i < SAMPLES; i++) mean += fft_input[i];
            mean /= SAMPLES;
            for (int i = 0; i < SAMPLES; i++) fft_input[i] -= mean;

            // ============================================================
            // ШАГ 3: ПРИМЕНЕНИЕ ОКНА ХЭММИНГА (убирает spectral leakage)
            // ============================================================
            dsps_mul_f32(fft_input, hamming_window, fft_input, SAMPLES, 1, 1, 1);

            // ============================================================
            // ШАГ 4: ПРЕОБРАЗОВАНИЕ В КОМПЛЕКСНЫЙ ФОРМАТ [Re, Im, Re, Im...]
            // ============================================================
            for (int i = SAMPLES - 1; i >= 0; i--) {
                temp_fft_buffer[i * 2] = fft_input[i];
                temp_fft_buffer[i * 2 + 1] = 0.0f;
            }

            // ============================================================
            // ШАГ 5: БЫСТРОЕ ПРЕОБРАЗОВАНИЕ ФУРЬЕ
            // ============================================================
            dsps_fft2r_fc32(temp_fft_buffer, SAMPLES);
            dsps_bit_rev_fc32(temp_fft_buffer, SAMPLES);
            dsps_cplx2reC_fc32(temp_fft_buffer, SAMPLES);

            // ============================================================
            // ШАГ 6: ВЫЧИСЛЕНИЕ МАГНИТУДЫ (амплитуды спектра)
            // ============================================================
            for (int i = 0; i < SAMPLES / 2; i++) {
                float re = temp_fft_buffer[i * 2];
                float im = temp_fft_buffer[i * 2 + 1];
                temp_fft_buffer[i] = sqrtf(re * re + im * im);
            }

            // ============================================================
            // ШАГ 7: ПОИСК ПИКОВ ПО 8 ЧАСТОТНЫМ ДИАПАЗОНАМ
            // ============================================================
            float peaks[NUM_BANDS] = {0};

            for (int b = 0; b < NUM_BANDS; b++) {
                int start_bin = (b == 0) ? start_bin_freq : (band_max_bin[b - 1] + 1);
                int end_bin = band_max_bin[b];

                for (int k = start_bin; k <= end_bin; k++) {
                    if (temp_fft_buffer[k] > peaks[b]) {
                        peaks[b] = temp_fft_buffer[k];
                    }
                }
            }

            // ============================================================
            // ШАГ 8: GATE ФИЛЬТР (TON/TOF для подавления импульсов)
            // ============================================================
            #ifdef ENABLE_GATE_FILTER
            for (int i = 0; i < NUM_BANDS; i++) {
                // TON: Включаем gate если сигнал > MIN_AMPLITUDE держится GATE_TON_MS
                if (gateTimerTON[i].TON(GATE_TON_MS, (peaks[i] > MIN_AMPLITUDE[i]))) {
                    channelGateActive[i] = true;
                }

                // TOF: Выключаем gate если сигнал <= MIN_AMPLITUDE держится GATE_TOF_MS
                if (!gateTimerTOF[i].TOF(GATE_TOF_MS, (peaks[i] > MIN_AMPLITUDE[i]))) {
                    channelGateActive[i] = false;
                }

                // Если gate выключен, обнуляем сырой пик
                if (!channelGateActive[i]) {
                    peaks[i] = 0;
                }
            }
            #endif

            // ============================================================
            // ШАГ 9: НОРМАЛИЗАЦИЯ В 0-255 С УЧЁТОМ ЧУВСТВИТЕЛЬНОСТИ
            // ============================================================
            float sensitivity_mult = (float)currentSettings.sensitivity / 100.0f;

            for (int i = 0; i < NUM_BANDS; i++) {
                // Масштабирование в 0-255
                float mapped_value = (peaks[i] - MIN_AMPLITUDE[i]) / (MAX_AMPLITUDE[i] - MIN_AMPLITUDE[i]) * 255.0f;

                // Применяем чувствительность
                mapped_value *= sensitivity_mult;

                // Ограничение 0-255
                if (mapped_value > 255.0f) mapped_value = 255.0f;
                if (mapped_value < 0.0f) mapped_value = 0.0f;

                uint8_t current_level = (uint8_t)mapped_value;

                // ============================================================
                // ШАГ 10: ДЕТЕКЦИЯ АТАКИ (резкий скачок уровня)
                // ============================================================
                band_data[i].isNewPeak = (current_level > last_level[i] + ATTACK_THRESHOLD);
                band_data[i].level = current_level;
                last_level[i] = current_level;
            }

            // ============================================================
            // ШАГ 11: ДЕТЕКТОР РЕЧИ (обнуление каналов при детекции речи)
            // ============================================================

            // Сумма высоких каналов (5, 6, 7)
            uint16_t high_sum = (band_data[5].level + band_data[6].level + band_data[7].level) / 3;

            // Сумма низких/средних каналов (0-4)
            uint16_t low_sum = (band_data[0].level + band_data[1].level + band_data[2].level + band_data[3].level + band_data[4].level) / 5;
            Serial.print("Низкие_sum: = ");Serial.println(low_sum);
            Serial.print("высокие_sum: = ");Serial.println(high_sum);
            // Речь = высокие почти нулевые И есть активность в низких
            bool isSpeech = false;
            if((high_sum < SPEECH_HF_THRESHOLD) && (low_sum > SPEECH_LF_MIN)) {
              isSpeech = true;
              Serial.print("детектор сработал");

              }
            else isSpeech = false;
            
            // Таймер речи: если речь держится > SPEECH_DETECT_TIMEOUT → обнуляем все каналы
            if (speechTimer.TON(SPEECH_DETECT_TIMEOUT, isSpeech)) {
                // Детектирована речь → обнуляем все уровни
                Serial.print("Обнуление каналов из-за речи");
                for (int i = 0; i < NUM_BANDS; i++) {
                    band_data[i].level = 0;
                    band_data[i].isNewPeak = false;
                }
            }

            // ============================================================
            // ШАГ 12: ОТПРАВКА ДАННЫХ В ЯДРО 1
            // ============================================================
            xQueueOverwrite(peaksQueue, &band_data);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
//****************************************************************************************
// --- Функция loop выполнения задачи ядра 1 ---
//****************************************************************************************
void loop() {
  //uint32_t loopStartTime = millis(); // Засекаем время в начале - дебаги
  
  // --- ЭТАП 1: Получение и обработка свежих данных ---
  static BandData band_data_copy[NUM_BANDS];
  xQueueReceive(peaksQueue, &band_data_copy, 0);

  // --- ЭТАП 1.5: Анализ звука для таймера бездействия ---
  int totalLevelForIdleCheck = 0;
  for (int i = 0; i < NUM_BANDS; i++) {
    totalLevelForIdleCheck += band_data_copy[i].level;
  }

  // Порог теперь в единицах 0-255. Если суммарный уровень всех каналов
  // больше, например, 50, считаем, что звук есть.
  const int IDLE_LEVEL_THRESHOLD = 50;
  bool soundPresent = (totalLevelForIdleCheck > IDLE_LEVEL_THRESHOLD);

  // --- ЭТАП 2: Обработка кнопок (возвращает true если ЛЮБАЯ кнопка нажата) ---
  bool anyButtonPressed = buttonsH();

  // --- ЭТАП 3: Логика отображения и сохранения ---
  // Проверяем, должны ли мы быть в меню ПРЯМО СЕЙЧАС
  // НЕ инвертируем! TOF теперь сам управляет логикой
  bool isMenuNow = menuTimeoutTimer.TOF(MENU_TIMEOUT, anyButtonPressed);

  // Используем триггер заднего фронта (FT) для отлова момента,
  // когда isMenuNow переключается из 'true' (были в меню) в 'false' (вышли из меню).
  // FT вернет true только ОДИН РАЗ в этот самый момент.
  if (menuExitTrigger.FT(isMenuNow)) {
    saveSettings(); // Сохраняем настройки ТОЛЬКО В МОМЕНТ ВЫХОДА из меню
    currentMenuMode = MENU_MODE_EFFECT; // АВТОМАТИЧЕСКИЙ ВОЗВРАТ в режим выбора эффекта!
    Serial.println("--- SETTINGS SAVED, MENU RESET TO EFFECT MODE ---");
  }

    
  

  static uint8_t auto_effect_index = 0;
    // Определяем, какой эффект рисовать СЕЙЧАС
  uint8_t effect_to_draw;

  if (currentSettings.currentEffect == 5) { // 5 - это "Авто"
    // Мы в режиме автопереключения
    if (autoCycleTimer.Gen(TIMERAUTOCYCLE)) {  
        // Прошло 3 секунды, переключаем на следующий эффект
        auto_effect_index = (auto_effect_index + 1) % 5; // 5 "реальных" эффектов
    }
    effect_to_draw = auto_effect_index; // Рисуем тот эффект, на который указывает наш счетчик
  } else {
    // Мы в обычном режиме, просто рисуем выбранный эффект
    effect_to_draw = currentSettings.currentEffect;
    // Сбрасываем таймер и счетчик авто-режима, чтобы при следующем входе
    // в "Авто" он начал с первого эффекта и полного интервала.
    autoCycleTimer.Reset();  // Изменено: TRes() -> Reset()
    auto_effect_index = 0;
  }

  // TOF с soundPresent: пока звук есть ИЛИ не прошло 20 сек БЕЗ звука -> активен
  if (idleTimer.TOF(IDLE_TIMEOUT, soundPresent)) {
    FlagIDLE = false;
    if (strip.canShow()) {

      strip.fillColor(backgr_colors[currentSettings.backgroundColor], BACKGROUND_BRIGHTNESS);
      switch(effect_to_draw){
          case 0:
            ColorMuzik(band_data_copy);
          break;
          case 1:
            effect_Stroboscope(band_data_copy);
            //strip.setPixelColor(1, RED, 255);
          break;
          case 2:
            effect_VuMeter_Gradient(band_data_copy);
            //strip.setPixelColor(2, RED, 255);
          break;
          case 3:
            //strip.setPixelColor(3, RED, 255);
            spawnSparks(band_data_copy);
            effect_DanceParty();
          break;
          case 4:
            //strip.setPixelColor(4, RED, 255);
            spawnStars(band_data_copy);
            effect_Stars();
          break;
      }
      
      strip.show();
    }
  }else {
    FlagIDLE = true;
    if(strip.canShow()){
      uint32_t comets_bg_color = strip.Color(
        ((uint16_t)backgr_colors_rgb[currentSettings.backgroundColor][0] * BACKGROUND_BRIGHTNESS) >> 8, // R
        ((uint16_t)backgr_colors_rgb[currentSettings.backgroundColor][1] * BACKGROUND_BRIGHTNESS) >> 8, // G
        ((uint16_t)backgr_colors_rgb[currentSettings.backgroundColor][2] * BACKGROUND_BRIGHTNESS) >> 8  // B
      );
      // Вызываем эффект "Кометы", передавая чистый цвет.
      strip.runCometsEffect(
        5,                        // количество комет 
        10,                       // длина хвоста комет
        channel_colors,           // цвета комет (используем цвета каналов)
        NUM_BANDS,                // количество каналов
        comets_bg_color,          // цвет фона (из настроек)
        1500                      // интервал появлния новых комет в мс
        );
      strip.show();
    }
  }
  
  // --- ЭТАП 4: Отрисовка на OLED дисплее ---
  if (FPS_Timer.Gen(25)) {                                // Ограничение до ~25 FPS
    oled.clear(); 
    // В зависимости от состояния, рисуем нужный экран
    if (isMenuNow) {
      Menu_OLED();
    } else {
      if(!FlagIDLE)Vizual_OLED(band_data_copy);
      else IDLE_OLED();
    }
    oled.display();                                     // Обновляем дисплей
  }

  ///--- Дебаги --- 
  //uint32_t loopExecutionTime = millis() - loopStartTime;
  //Serial.print("Loop time: ");
  //Serial.println(loopExecutionTime);
  vTaskDelay(pdMS_TO_TICKS(1));
}
//****************************************************************************************
// --- ФУНКЦИИ ЭФФЕКТОВ ---                                                              *
//****************************************************************************************
//****************************************************************************************
// --- Эффект "ЦветМузыка" с ВСТРОЕННЫМ аналоговым затуханием (версия для BandData) ---
//****************************************************************************************
void ColorMuzik(BandData* band_data) {

  // --- Переменные, которые "живут" только внутри этого эффекта ---
  static uint8_t display_levels[NUM_BANDS] = {0};
  
  // --- Константы для настройки эффекта ---
  const uint8_t FADE_SPEED = 15;
  const uint8_t BRIGHTNESS_THRESHOLD = 30;

  // --- Шаг 1: Логика сглаживания ---
  for (int i = 0; i < NUM_BANDS; i++) {
    uint8_t raw_level = band_data[i].level * 2.5;
    if (raw_level > display_levels[i]) {
      display_levels[i] = raw_level;
    } else {
      if (display_levels[i] > FADE_SPEED) {
        display_levels[i] -= FADE_SPEED;
      } else {
        display_levels[i] = 0;
      }
    }
  }

  // --- Шаг 2: Отрисовка на ленте ---
  for (int i = 0; i < NUM_LEDS; i++) {
    int channel = i % NUM_BANDS;
    uint8_t brightness = display_levels[channel]; 
    if (brightness > BRIGHTNESS_THRESHOLD) {
      strip.setPixelColor(i, channel_colors[channel], brightness);
    }// else {
     // strip.setPixelColor(i, BLACK, 0);
    //}
  }
}
//****************************************************************************************
// --- Эффект стробоскоп ---
//****************************************************************************************
void effect_Stroboscope(BandData* band_data) {

    // --- Внутренний, самодостаточный детектор бита ---
    
    static float bass_history[15] = {0}; // Короткая история для резкости
    static int history_index = 0;
    const float BEAT_THRESHOLD = 2.0f; // Порог (в 2 раза громче среднего)

    // 1. Берем текущий УРОВЕНЬ басовых каналов (уже обработанный АРУ!)
    // Складываем, чтобы получить общую энергию баса
    float current_bass_level = band_data[0].level + band_data[1].level;

    // 2. Считаем среднее по истории
    float average_bass = 0;
    for (int i = 0; i < 15; i++) {
        average_bass += bass_history[i];
    }
    average_bass /= 15;

    // 3. Обновляем историю
    bass_history[history_index] = current_bass_level;
    history_index = (history_index + 1) % 15;

    // 4. Детектируем "удар"
    //bool beat_detected = (current_bass_level > average_bass * BEAT_THRESHOLD) && (current_bass_level > 50); // Доп. порог, чтобы не срабатывать в тишине
    if ((current_bass_level > average_bass * BEAT_THRESHOLD) && (current_bass_level > 100)) {
        strip.fill(WHITE);
        return; 
    }
    // 5. Находим максимальный уровень среди ВСЕХ каналов для фона
    uint8_t max_level = 0;
    for (int i = 0; i < NUM_BANDS; i++) {
        if (band_data[i].level > max_level) {
            max_level = band_data[i].level;
        }
    }
    // 6. Берем базовый цвет фона из массива backgr_colors[currentSettings.backgroundColor];
    strip.fillColor(backgr_colors[currentSettings.backgroundColor],constrain(max_level, BACKGROUND_BRIGHTNESS, 255));
}
//****************************************************************************************
// --- Эффект "VU-метр" (ОПТИМИЗИРОВАННЫЙ) ---
//****************************************************************************************

void effect_VuMeter_Gradient(BandData* band_data) {
    
    // --- Настройки эффекта ---
    const int NUM_REPEATS = 2;   // Сколько раз повторить VU-метр на ленте
    const int FADE_SPEED = 1;    // Скорость затухания (в пикселях за кадр)

    // --- Переменная состояния (хранит "видимую" длину) ---
    static int displayLength = 0;
    
    // --- Шаг 1: Получаем "громкость" ---
    // Находим максимальный .level (0-255) среди всех каналов.
    uint8_t max_level = 0;
    for (int i = 0; i < NUM_BANDS; i++) {
        if (band_data[i].level > max_level) {
            max_level = band_data[i].level;
        }
    }

    // --- Шаг 2: Расчет "целевой" длины ---
    const int segmentLength = NUM_LEDS / NUM_REPEATS;
    if (segmentLength == 0) return;
    const int halfSegment = segmentLength / 2;

    int realLength = map(max_level, 0, 255, 0, halfSegment);

    // --- Шаг 3: Логика затухания ("Атака/Затухание") ---
    if (realLength > displayLength) {
        // Атака: мгновенно поднимаем до нового уровня
        displayLength = realLength;
    } else {
        // Затухание: плавно опускаем
        if (displayLength > FADE_SPEED) {
            displayLength -= FADE_SPEED;
        } else {
            displayLength = 0;
        }
    }
    // Ограничиваем на всякий случай
    displayLength = constrain(displayLength, 0, halfSegment);

    // --- Шаг 4: Отрисовка ---
    // Эффект сам очищает фон, т.к. рисует всю ленту
   // strip.clear();
    
    for (int r = 0; r < NUM_REPEATS; r++) {
        // Вычисляем центр ТЕКУЩЕГО сегмента
        int segmentStart = r * segmentLength;
        int centerPixel = segmentStart + halfSegment;

        // Рисуем правую половину градиента
        strip.rainbowStatic(
            centerPixel,        // Начальный пиксель
            displayLength,      // Текущая "видимая" длина
            false,              // Направление: вперед
            255,                // Яркость
            170,                // Начальный тон (бирюзовый)
            0                   // Конечный тон (красный)
        );
        
        // Рисуем левую половину градиента
        strip.rainbowStatic(
            centerPixel,    // Начальный пиксель 
            displayLength,      // Текущая "видимая" длина
            true,               // Направление: назад
            255,                // Яркость
            170,                // Начальный тон (бирюзовый)
            0                   // Конечный тон (красный)
        );
    }
}
//****************************************************************************************
// --- Функция "Рождения" искр для Dance Party ---
// Анализирует данные и создает новые "искры" при появлении новых пиков.
//****************************************************************************************
void spawnSparks(BandData* band_data) {
  
  const uint8_t SPAWN_THRESHOLD = 15; 

  for (int i = 0; i < NUM_BANDS; i++) {
    if (channel_triggers[i].RT(band_data[i].isNewPeak) && band_data[i].level > SPAWN_THRESHOLD) {  // Изменено: Rtr() -> RT()
      for (int j = 0; j < N_SPARKS; j++) {
        if (!sparks[j].active) {
          
          sparks[j].active = true;
          sparks[j].age = 0;
          sparks[j].color = channel_colors[i];
          
          // --- Расчет СКОРОСТИ (комбинированный) ---
          uint8_t magnitude = constrain(band_data[i].level,127,255);
          float base_speed = base_speed_by_channel[i];
          float speed_bonus = (magnitude / 255.0f) * 0.1f;  
          sparks[j].speed = base_speed + speed_bonus;
          //sparks[j].speed = base_speed_by_channel[i];
          //sparks[j].speed = (magnitude / 255.0f) * 0.8f;//0.5f;
          // --- КОНЕЦ БЛОКА ---

          break; 
        }
      }
    }
  }
}
//****************************************************************************************
// --- Эффект "Танцы плюс" (Dance Party) ---
// Обновляет и отрисовывает "искры".
//****************************************************************************************
void effect_DanceParty() {
  
  // Цикл обновления и отрисовки
  for (int i = 0; i < N_SPARKS; i++) {
    if (sparks[i].active) {
      
      // 1. Увеличиваем возраст
      sparks[i].age++;

      // 2. Позиция = возраст * постоянная_скорость
      int pos = (int)(sparks[i].age * sparks[i].speed);

      // 3. --- ЯРКОСТЬ ПОСТОЯННА ---
      // Используем максимальную яркость 255
      //const uint8_t BRIGHTNESS = 255;
      
      // 4. Отрисовываем симметрично от центра
      int center = NUM_LEDS / 2;
      if (center + pos < NUM_LEDS) strip.setPixelColor(center + pos, sparks[i].color, map(pos, 0, NUM_LEDS / 2, 255, 30));
      if (center - 1 - pos >= 0) strip.setPixelColor(center - 1 - pos, sparks[i].color, map(pos, 0, NUM_LEDS / 2, 255, 30));

      // 5. --- СМЕРТЬ ТОЛЬКО ОТ УДАРА О КРАЙ ---
      if ((center + pos) >= NUM_LEDS) {
        sparks[i].active = false;
      }
    }
  }
}
//****************************************************************************************
// --- Функция "Рождения" звезд для эффекта "Звезды" ---
// Реагирует на новые пики и создает "звезды" в случайных местах.
//****************************************************************************************
void spawnStars(BandData* band_data) {
  const uint8_t SPAWN_THRESHOLD = 40; // Порог .level для рождения звезды

  // Проходим по всем 8 каналам
  for (int i = 0; i < NUM_BANDS; i++) {
    // Если триггер сработал на новый пик...
    if (channel_triggers[i].RT(band_data[i].isNewPeak) && band_data[i].level > SPAWN_THRESHOLD) {  // Изменено: Rtr() -> RT()
      
      // ...ищем свободный слот для новой звезды
      for (int j = 0; j < MAX_STARS; j++) {
        if (!starPool[j].active) {
          // Нашли! Заполняем данными.
          starPool[j].active = true;
          starPool[j].position = random(NUM_LEDS);
          // Начальная яркость звезды = сила пика
          starPool[j].brightness = 255;//band_data[i].level; 
          starPool[j].color = channel_colors[i];
          
          // Выходим из внутреннего цикла, чтобы не искать больше слотов для этого канала
          break; 
        }
      }
    }
  }
}
//****************************************************************************************
// --- Эффект "Звезды" ---
// Обновляет и отрисовывает звезды поверх существующего фона.
//****************************************************************************************
void effect_Stars() {
  
  const int FADE_SPEED = 5; // Скорость затухания (чем больше, тем быстрее)

  // Цикл обновления и отрисовки
  for (int i = 0; i < MAX_STARS; i++) {
    // Работаем только с активными звездами
    if (starPool[i].active) {
      
      // 1. Рисуем звезду с ее текущей яркостью ПОВЕРХ фона
      strip.setPixelColor(starPool[i].position, starPool[i].color, starPool[i].brightness);

      // 2. Уменьшаем яркость для следующего кадра
      if (starPool[i].brightness > FADE_SPEED) {
        starPool[i].brightness -= FADE_SPEED;
      } else {
        starPool[i].brightness = 0; // Погасла
      }
      
      // 3. Если звезда погасла, "убиваем" ее
      if (starPool[i].brightness == 0) {
        starPool[i].active = false;
      }
    }
  }
}