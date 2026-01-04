//************************************************************************
// Диапазон частот (Гц): С учетом вашего требования "без перекрытий".
// Центральная частота (Гц): Середина диапазона, как вы и просили.
// Границы в "бинах" FFT: Самое важное для реализации в коде. (Напомню, 1 бин ≈ 23.83 Гц).
// Канал	Диапазон частот (Гц)	Центральная частота (Гц)	Описание	Верхняя граница (в "бинах" FFT)
// 1	150 - 259 Гц	205 Гц	Глубокий бас	259 / 23.83 ≈ 10
// 2	260 - 449 Гц	355 Гц	Основной бас, "тело" звука	449 / 23.83 ≈ 18
// 3	450 - 779 Гц	615 Гц	Низкая середина, теплота	779 / 23.83 ≈ 32
// 4	780 - 1349 Гц (1.3 кГц)	1065 Гц	Середина, основа инструментов	1349 / 23.83 ≈ 56
// 5	1.35 - 2.33 кГц	1.84 кГц	Верхняя середина, разборчивость	2330 / 23.83 ≈ 97
// 6	2.34 - 4.04 кГц	3.19 кГц	"Присутствие", яркость	4040 / 23.83 ≈ 169
// 7	4.05 - 7.0 кГц	5.52 кГц	Высокие частоты, "блеск"	7000 / 23.83 ≈ 293
// 8	7.01 - 12.2 кГц	9.6 кГц	Самый верх, "воздух"	12200 / 23.83 ≈ 511
//************************************************************************
#include "SavaOLED_ESP32.h"                                // библиотека дисплея OLED
#include "SavaGFX_OLED.h"                                  // библиотека графики для OLED
#include "Fonts/SF_Font_P8.h"                              // шрифт 8px
#include "Fonts/SF_Font_x2_P16.h"                          // шрифт 16px
#include "esp_dsp.h"                                       // Подключаем основную библиотеку для цифровой обработки сигналов на ESP32
#include <math.h>                                          // Подключаем математическую библиотеку для использования константы M_PI (число Пи)
#include "esp_timer.h"
#include <SavaTrig.h>                                      // Библиотека тригеров
#include <SavaTime.h>                                      // Библиотека таймеров
#include <SavaButton.h>                                    // Библиотека кнопок
#include <EEPROM.h>                                        // библиотека памяти
#include <SavaLED_ESP32.h>
SavaOLED_ESP32 oled(128, 64);                              // объявляем дисплей
SavaGFX_OLED gfx(&oled);                                   // объявляем графику
#define SAMPLES                 512 //1024                 // Количество семплов
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
const float FADE_SPEED = 2.0f; // <<< ВОЗВРАЩАЕМ КОНСТАНТУ. Чем БОЛЬШЕ, тем БЫСТРЕЕ падают.
int displayBarHeights[NUM_BANDS] = {0};                       // Массив для хранения текущей "видимой" высоты столбиков (для плавного затухания)
int peakBarHeights[NUM_BANDS] = {0};                          // Массив для хранения высоты пиковых индикаторов
// --- Переменные для управления экранами OLED ---
SavaTime menuTimeoutTimer;                                    // Создаем экземпляр таймера
SavaTrig menuExitTrigger;                                     // Триггер для отслеживания момента выхода из меню
SavaTrig channel_triggers[NUM_BANDS];
const uint32_t MENU_TIMEOUT = 5000;                           // 5 секунд бездействия окне меню
bool FlagIDLE = false;
SavaTime FPS_Timer;
const float band_gain_factors[8] = {1.5f, 0.4f, 0.6f, 0.9f, 1.3f, 1.0f, 1.1f, 20.9f}; //усилениеие, ослабление по каналам
uint32_t SetSensitivity;
//***************************************************************************************
const uint32_t channel_colors[NUM_BANDS] = {
  //RED, ORANGE, YELLOW, BLUE, CYAN, SKYBLUE, LIME, GREEN      // Цвета для каналов
  RED,GREEN,WHITE,YELLOW,BLUE,ORANGE,LIME,CYAN
  };
const uint32_t backgr_colors[NUM_BG_OPTIONS] = {
  LIME, RED, BLUE, WHITE, BLACK                           // 4 цвета + "Выкл"
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
QueueHandle_t peaksQueue; // Наша очередь для передачи данных
// --- Переменные, используемые только на ядре 0 ---
esp_timer_handle_t sampling_timer;
portMUX_TYPE isrMux = portMUX_INITIALIZER_UNLOCKED;           // portMUX для ISR, он легковеснее
volatile uint16_t raw_samples[SAMPLES];
volatile uint16_t sample_index = 0;
volatile bool samplesReadyFlag = false;                       // Текущий индекс в буфере raw_samples
//****************************************************************************************
//const int band_max_bin[8] = {10, 18, 32, 56, 97, 169, 293, 511};  // Массив с верхними границами каждого из 8 каналов в "бинах" FFT. 1024
const int band_max_bin[8] = {5, 7, 14, 28, 56, 112, 224, 255};  // Массив с верхними границами каждого из 8 каналов в "бинах" FFT. 512
//****************************************************************************************
// --- Настройки чувствительности ---
//const long MIN_AMPLITUDE = 10000;                             // Минимальная амплитуда (порог шума, статичный).
//*****************************************************************************************
// --- Переменные и константы для АРУ и фильтрации ---
//*****************************************************************************************
long AnalogVolume; // Аналоговое значение входное отфильтрованное по нижнему уровню
const int N_FRAMES = 5; // Количество кадров для усреднения в АРУ

// Порог "дельты", ниже которого изменение считается шумом.
// Значения гораздо ниже, т.к. мы работаем с разницей, а не абсолютным уровнем.
const long DELTA_THRESHOLD = 500;

// Минимальная ширина диапазона для АРУ "дельты".
const long DELTA_MIN_RANGE = 4000;

// Начальный "потолок" для АРУ "дельты" при старте.
const long DELTA_INITIAL_MAX = 8000;

// Массивы для хранения истории и динамических порогов (используются только в ядре 0)
long band_history[NUM_BANDS][N_FRAMES];
long minLvlAvg[NUM_BANDS];
long maxLvlAvg[NUM_BANDS];

// Переменные для "липких" пиков
uint8_t bandPeakLevel[NUM_BANDS];
uint8_t bandPeakCounter = 0;
const uint8_t bandPeakDecay = 1; // Скорость падения пиков
const uint8_t PEAK_FALL_AMOUNT = 5;//10; // Величина падения
// Индивидуальный порог "тишины" для каждого из 8 каналов
const long noise_thresholds_by_band[NUM_BANDS] = {
  20000,  // Макс. шум был ~19355
  17000,   // Макс. шум был ~6768
  13000,   // Макс. шум был ~4615
  6000,   // Макс. шум был ~5929
  3000,   // Макс. шум был ~2507
  3000,   // Макс. шум был ~2507 (ошибка в предыдущем анализе)
  3500,   // Макс. шум был ~1338
  2500    // Макс. шум был ~931
};
/*
// Минимальная ширина динамического диапазона для каждого канала.
// Предотвращает "зашкаливание" на тихих звуках.
const long min_range_by_band[NUM_BANDS] = {
  70000,   // Канал 1
  100000,  // Канал 2
  80000,   // Канал 3
  30000,   // Канал 4
  35000,   // Канал 5
  30000,   // Канал 6
  45000,   // Канал 7
  5000    // Канал 8
};

// Начальный "потолок" для АРУ при старте программы.
const long initial_max_lvl_by_band[NUM_BANDS] = {
  90000,   // Канал 1
  380000,  // Канал 2
  100000,  // Канал 3
  40000,   // Канал 4
  45000,   // Канал 5
  35000,   // Канал 6
  50000,   // Канал 7
  40000    // Канал 8
};*/
// =========================================================================
// --- Структура для передачи обработанных данных между ядрами ---
// =========================================================================
struct BandData {
  uint8_t level;     // Нормализованный уровень для этого канала (0-255)
  uint8_t peakLevel; // Уровень "липкого" пика для этого канала (0-255)
  bool isNewPeak;    // Флаг: был ли в этом кадре новый пик?
};
//*****************************************************************************************
//Структуры
struct Settings { 
  uint8_t brightness, currentEffect, sensitivity, backgroundColor; 
  uint16_t numLeds; 
  uint8_t smooth; 
  uint16_t magic_key; 
  };
Settings currentSettings;

//*****************************************************************************************
enum MenuItem { 
  MENU_BRIGHTNESS, 
  MENU_EFFECT, 
  MENU_SENSITIVITY, 
  MENU_BACKGROUND, 
  MENU_SMOOTH };
MenuItem currentMenuItem = MENU_EFFECT;
//****************************************************************************************
// --- Структура и массив для эффекта "Танцы плюс" ("Искры") ---
//****************************************************************************************
#define N_SPARKS 30 // Максимальное количество "искр" на экране

struct Spark {
  bool active;      // Активна ли искра?
  //uint8_t magnitude;  // Сила (0-255), влияет на скорость
  //int duration;      // Время жизни в кадрах
  float speed; // Индивидуальная скорость "искры"
  int age;          // Возраст в кадрах
  uint32_t color;     // Цвет искры
};

Spark sparks[N_SPARKS]; // Глобальный массив-"инкубатор" для искр

// Базовая скорость "искры" в пикселях за кадр, в зависимости от канала (частоты)
const float base_speed_by_channel[NUM_BANDS] = {
  0.18f,  // Канал 0 (бас) - медленные
  0.20f,
  0.22f,
  0.26f,  // Канал 3 (середина)
  0.28f,
  0.30f,
  0.32f,
  0.34f   // Канал 7 (верх) - быстрые
};
//****************************************************************************************
// --- Структура и массив для эффекта "Звезды" ---
//****************************************************************************************
#define MAX_STARS 40 // Максимальное количество "звезд" на экране

struct Star {
  bool active;      // Активна ли звезда?
  int position;     // Позиция на ленте
  int brightness;   // Текущая яркость (0-255)
  uint32_t color;     // Цвет звезды
};

Star starPool[MAX_STARS]; // Глобальный массив-"пул" для звезд
//****************************************************************************************
//--- прототипы функций ---
//****************************************************************************************
void Vizual_OLED(BandData* band_data_copy);
void Menu_OLED();
void IDLE_OLED();
bool buttonsH();
void saveSettings();
void loadSettings();
void ColorMuzik(BandData* band_data);
void effect_Stroboscope(BandData* band_data);
void effect_VuMeter_Gradient(BandData* band_data);
void spawnSparks(BandData* band_data);
void effect_DanceParty();
void spawnStars(BandData* band_data);
void effect_Stars();
//*****************************************************************************************
// --- Функция отрисовки анализатора на OLED дисплее ---
//*****************************************************************************************
void Vizual_OLED(BandData* band_data_copy) {

    // --- Подготовка массива уровней для SavaGFX эквалайзера ---
    static uint8_t levels[NUM_BANDS] = {0};

    for (int i = 0; i < NUM_BANDS; i++) {
        levels[i] = band_data_copy[i].level;  // Копируем уровни (0-255)
    }

    // --- Отрисовка с контролем FPS ---
    if (FPS_Timer.Gen(25)) {  // Изменено: GenML() -> Gen()
        oled.clear();

        // Используем готовый эквалайзер из SavaGFX_OLED
        // peaks=true, peakDecaySpeed=10 (100ms на уровень)
        gfx.equalizer8(levels, true, 10);

        oled.display();  // Изменено: update() -> display()
    }
}
//*****************************************************************************************
// --- Функция отрисовки меню на OLED дисплее ---
//*****************************************************************************************
void Menu_OLED() {
    // --- Шаг 1: Очищаем экран и устанавливаем шрифт ---
    oled.clear();
    oled.font(SF_Font_x2_P16); // Крупный шрифт 16px для меню

    // --- Шаг 2: Отрисовка названия параметра ---
    oled.cursor(0, 8, StrCenter);

    switch (currentMenuItem) {
        case MENU_BRIGHTNESS:   oled.print("Яркость");   break;
        case MENU_EFFECT:       oled.print("Эффект");      break;
        case MENU_SENSITIVITY:  oled.print("Чувств.");   break;
        case MENU_BACKGROUND:   oled.print("Фон");         break;
        case MENU_SMOOTH:       oled.print("Плавность");  break;
    }
    oled.drawPrint(); // ОБЯЗАТЕЛЬНО после print()

    // --- Шаг 3: Отрисовка значения параметра ---
    oled.cursor(0, 40, StrCenter);

    switch (currentMenuItem) {
        case MENU_BRIGHTNESS:
            oled.print(currentSettings.brightness);
            oled.print("%");
            break;
        case MENU_EFFECT: {
            const char* effectNames[] = { "ЦветМузыка", "Стробоскоп", "VU-Метр", "Искры+", "Звезды", "Авто" };
            oled.print(effectNames[currentSettings.currentEffect]);
            break;
        }
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
    oled.drawPrint(); // ОБЯЗАТЕЛЬНО после print()

    // --- Шаг 4: Обновляем физический экран ---
    oled.display();  // Изменено: update() -> display()
}
//*****************************************************************************************
// --- Функция отрисовки меню на OLED дисплее ---
//*****************************************************************************************
void IDLE_OLED(){
    oled.clear();
    oled.font(SF_Font_x2_P16); // Крупный шрифт 16px
    oled.cursor(0, 28, StrCenter);
    oled.print("ТИШИНА");
    oled.drawPrint(); // ОБЯЗАТЕЛЬНО после print()
    oled.display();  // Изменено: update() -> display()
}
//*****************************************************************************************
// ---управление кнопками---
//*****************************************************************************************
bool buttonsH(){

  bool butMinus = trigRT_minus.RT(btn_minus.read());
  bool butPlus = trigRT_plus.RT(btn_plus.read());
  bool butOk = trigRT_ok.RT(btn_ok.read());
  bool anyButton = butMinus || butPlus || butOk;

  // Обрабатываем логику только если меню активно!
  // TOF теперь требует input: anyButton продлевает таймер
  if (menuTimeoutTimer.TOF(MENU_TIMEOUT, anyButton)) {
    if(butOk){
      currentMenuItem = (MenuItem)((currentMenuItem + 1) % NUM_MENU_ITEMS);
    }
    if (butPlus) {
      switch (currentMenuItem) {
        case MENU_BRIGHTNESS:
         if (currentSettings.brightness < 95) currentSettings.brightness += 5;
          else currentSettings.brightness = 100;
          break;
        case MENU_EFFECT: currentSettings.currentEffect = (currentSettings.currentEffect + 1) % NUM_EFFECTS;
        break;
        case MENU_SENSITIVITY: if (currentSettings.sensitivity < 100) currentSettings.sensitivity += 10;
        break;
        case MENU_BACKGROUND: currentSettings.backgroundColor = (currentSettings.backgroundColor + 1) % NUM_BG_OPTIONS;
        break;
        case MENU_SMOOTH:
          if (currentSettings.smooth < 100) currentSettings.smooth += 10;
        break;
        }
    }
    if (butMinus) {
      switch (currentMenuItem) {
        case MENU_BRIGHTNESS:
          if (currentSettings.brightness > 55) currentSettings.brightness -= 5;
          else currentSettings.brightness = 50;
        break;
        case MENU_EFFECT:
          if (currentSettings.currentEffect > 0) currentSettings.currentEffect--;
          else currentSettings.currentEffect = NUM_EFFECTS - 1;
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
    strip.setBrightness(map(currentSettings.brightness,50,100,127,255));
    SetSensitivity = map(currentSettings.sensitivity,0,100,80000,1);
  }

  return anyButton;
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
  
  // --- Инициализация кнопок через SavaButton ---
  btn_minus(BTN_MINUS_PIN, PLUS);  // GPIO13, подтяжка к плюсу
  btn_ok(BTN_OK_PIN, PLUS);        // GPIO15, подтяжка к плюсу
  btn_plus(BTN_PLUS_PIN, PLUS);    // GPIO5, подтяжка к плюсу

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.begin(115200);

  // --- ИНИЦИАЛИЗАЦИЯ И ЗАГРУЗКА НАСТРОЕК ---
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();

  // --- Инициализация OLED (I2C автоматически настраивается) ---
  oled.init(800000, 21, 22);  // 800kHz, SDA=21, SCL=22 (стандарт ESP32)
  oled.clear();

  // --- Заставка SAVA ---
  oled.font(SF_Font_x2_P16);       // Крупный шрифт (16px недостаточно для scale(4), но покажем)
  oled.cursor(0, 28, StrCenter);    // Позиция для текста
  oled.drawMode(ADD_UP);           // Режим наложения
  oled.print("SAVA");
  oled.drawPrint();
  oled.rectR(0, 0, 127, 63, 3, REPLACE);  // Прямоугольник со скруглением r=3
  oled.display();
  delay(3000);
  oled.clear();

  // --- Инициализация LED ленты ---
  if (!strip.begin(NUM_LEDS, LED_PIN)) {
    while (true);
  }
  strip.setGammaCorrection(true);
  // --- Инициализация библиотеки esp-dsp ---
  // Выполняется один раз для подготовки внутренних таблиц, что ускоряет FFT.
  esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
  if (ret != ESP_OK) {
    Serial.println("FFT initialization failed!");
    return;
  }
    //создаем очередь
    //peaksQueue = xQueueCreate(1, sizeof(float) * NUM_BANDS);
    peaksQueue = xQueueCreate(1, sizeof(BandData) * NUM_BANDS);

    // Создаем и запускаем задачу для FFT на ядре 0
    xTaskCreatePinnedToCore(
        TaskFFTcode, "TaskFFT", 32768, NULL, 10, NULL, 0);

    Serial.println("Setup complete.");
  delay(500);
  //disableCore0WDT();
  //disableCore1WDT();
}
//****************************************************************************************
// --- Функция выполнения задачи ядра 0: Сбор, FFT, vTaskDelay(1) ---
//****************************************************************************************
void TaskFFTcode(void * pvParameters) {
    Serial.print("TaskFFT running on core ");
    Serial.println(xPortGetCoreID());

    memset(bandPeakLevel, 0, sizeof(bandPeakLevel));
    // --- Инициализация переменных для АРУ ---
    memset(band_history, 0, sizeof(band_history));
    for (int i = 0; i < NUM_BANDS; i++) {
        minLvlAvg[i] = 0;
        maxLvlAvg[i] = DELTA_INITIAL_MAX;
    }


    float fftSamples[SAMPLES]; // Локальный буфер для работы
    static float wind_buffer[SAMPLES];
    dsps_wind_hann_f32(wind_buffer, SAMPLES);
   

    // Настройка и запуск esp_timer ПРЯМО ВНУТРИ ЗАДАЧИ
    const esp_timer_create_args_t timer_args = {
        .callback = &sampling_timer_callback,
        .name = "audio_sampler"
    };
    esp_timer_create(&timer_args, &sampling_timer);
    uint64_t period_us = 1000000 / SAMPLING_FREQ;
    esp_timer_start_periodic(sampling_timer, period_us);

    int start_bin_freq = (int)round(150.0 / (SAMPLING_FREQ / (float)SAMPLES)); //разгружаем

    //float temp_fft_buffer[2048];                      
    float temp_fft_buffer[SAMPLES * 2]; // убрал чтоб не нагружать процессор 
    float fft_buffer[SAMPLES]; 
    
    //SetSensitivity = map(currentSettings.sensitivity,0,100,1,20000);

    for (;;) {
        // Если флаг поднят, начинаем работу
        if (samplesReadyFlag) {
            
            // 1. Быстро копируем данные и сбрасываем флаг под защитой
            portENTER_CRITICAL(&isrMux);
            //samplesReadyFlag = false;
            for (int i = 0; i < SAMPLES; i++) {
                //AnalogVolume = constrain(map(raw_samples[i],500,4096,0,4096),0,4096);
                AnalogVolume = raw_samples[i];
                fftSamples[i] = (float)AnalogVolume; 
            }
            samplesReadyFlag = false;
            portEXIT_CRITICAL(&isrMux);
        }
            //Serial.print(AnalogVolume); Serial.print(" - ");  

            // 2. Выполняем полный расчет FFT (все буферы локальные)
            
            //dsps_mul_f32(fftSamples, wind_buffer, temp_fft_buffer, SAMPLES, 1, 1, 1);
            for (int i = SAMPLES - 1; i >= 0; i--) {
                temp_fft_buffer[i * 2] = fftSamples[i];// temp_fft_buffer[i];
                temp_fft_buffer[i * 2 + 1] = 0;
            }

            dsps_fft2r_fc32(temp_fft_buffer, SAMPLES);
            dsps_bit_rev_fc32(temp_fft_buffer, SAMPLES);
            dsps_cplx2reC_fc32(temp_fft_buffer, SAMPLES);

            for (int i = 0 ; i < SAMPLES / 2 ; i++) {
              fft_buffer[i] = sqrt(temp_fft_buffer[i * 2 + 0] * temp_fft_buffer[i * 2 + 0] + temp_fft_buffer[i * 2 + 1] * temp_fft_buffer[i * 2 + 1]);
              //Serial.print(fft_buffer[i]);Serial.print(" ");
            }

            //int start_bin_freq = 6; 
            //int start_bin_freq = (int)round(150.0 / (SAMPLING_FREQ / (float)SAMPLES)); //разгружаем
            // --- Шаг 3.1: Расчет "сырых" пиков (ваш код) ---
            float raw_peaks[NUM_BANDS] = {0};
            //int start_bin_freq = 6;
            for (int b = 0; b < NUM_BANDS; b++) {
                int start_bin = (b == 0) ? start_bin_freq : (band_max_bin[b-1] + 1);
                int end_bin = band_max_bin[b];
                for (int k = start_bin; k <= end_bin; k++) {
                    if (temp_fft_buffer[k] > raw_peaks[b]) {
                        raw_peaks[b] = temp_fft_buffer[k];
                    }
                }
            }

            // --- Шаг 3: Фильтрация и отправка ---
            for (int i = 0; i < NUM_BANDS; i++) {
                // Применяем gain
                //raw_peaks[i] *= band_gain_factors[i];
                
                // Отсекаем шум
                if (raw_peaks[i] < noise_thresholds_by_band[i]) {
                    raw_peaks[i] = 0;
                }
            }

            // 4. Отправляем в очередь "сырые", но очищенные float
            xQueueOverwrite(peaksQueue, &raw_peaks);
        //} //скоба флага 
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
    Serial.println("--- SETTINGS SAVED ---"); // Отладочное сообщение
  }

    
  

  static uint8_t auto_effect_index = 0;
    // Определяем, какой эффект рисовать СЕЙЧАС
  uint8_t effect_to_draw;

  if (currentSettings.currentEffect == 5) { // 5 - это "Авто"
    // Мы в режиме автопереключения
    if (autoCycleTimer.Gen(TIMERAUTOCYCLE)) {  // Изменено: GenML() -> Gen()
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
        5,                      // num_comets
        10,                     // tail_length
        channel_colors,         // palette
        NUM_BANDS,              // palette_size
        comets_bg_color,        // background_color
        1500                    // spawn_interval_ms
        );
      strip.show();
    }
  }
  
  // В зависимости от состояния, рисуем нужный экран
  if (isMenuNow) {
    Menu_OLED();
  } else {
    if(!FlagIDLE)Vizual_OLED(band_data_copy);
    else IDLE_OLED();
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
    // 5. Находим максимальный "липкий" пик среди ВСЕХ каналов.
    // Это будет наш уровень громкости для фона.
    uint8_t max_peak_level = 0;
    for (int i = 0; i < NUM_BANDS; i++) {
        if (band_data[i].peakLevel > max_peak_level) {
            max_peak_level = band_data[i].peakLevel;
        }
    }
    //Serial.println(max_peak_level);
    // 6. Берем базовый цвет фона из массива backgr_colors[currentSettings.backgroundColor]; 
    strip.fillColor(backgr_colors[currentSettings.backgroundColor],constrain(max_peak_level, BACKGROUND_BRIGHTNESS, 255));
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
          //float base_speed = base_speed_by_channel[i];
          //float speed_bonus = (magnitude / 255.0f) * 0.5f;  
          //sparks[j].speed = base_speed + speed_bonus;
          //sparks[j].speed = base_speed_by_channel[i];
          sparks[j].speed = (magnitude / 255.0f) * 0.8f;//0.5f;
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