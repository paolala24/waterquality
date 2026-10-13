/**
 * @file main.cpp
 * @brief Sistema de Monitoreo de Calidad del Agua basado en ESP32
 * @details Sistema completo de adquisición de datos de sensores de calidad del agua
 *          (temperatura, pH, TDS, turbidez) con almacenamiento en RTC Memory persistente,
 *          gestión de deep sleep para bajo consumo, envío de datos mediante WiFi/WebSocket,
 *          sincronización de RTC externo MAX31328, y monitoreo de salud del sistema con
 *          watchdog. Diseñado para operación autónoma prolongada con ciclos de medición
 *          configurables y transmisión de datos por solicitud del servidor.
 *
 * Arquitectura:
 * - Ciclo de deep sleep de 80 segundos
 * - Tiempo activo de 45 segundos para lecturas múltiples de sensores
 * - Almacenamiento persistente en RTC Memory (sobrevive deep sleep)
 * - Verificación WiFi cada N lecturas (configurable)
 * - Modo manual: Espera solicitud del servidor para descargar datos
 * - Watchdog hardware/software para recuperación ante fallos
 * - RTC externo para timestamps precisos
 *
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#include <Arduino.h>
#include "WatchDogManager.h"
#include "RTCMemory.h"
#include "DeepSleepManager.h"
#include "Temperatura.h"
#include "TDS.h"
#include "Turbidez.h"
#include "WifiManager.h"
#include "RTC.h"
#include "pH.h"
#include "CalibrationManager.h"

// ——— Configuración del Sistema ———

/**
 * @def SLEEP_INTERVAL_SECONDS
 * @brief Intervalo de deep sleep en segundos entre ciclos de medición
 * @details Tiempo que el ESP32 permanece en deep sleep para ahorro de energía.
 *          Consumo típico: ~10µA en deep sleep vs ~80mA activo.
 * @note Valor típico: 60-300 segundos según aplicación.
 */
#define SLEEP_INTERVAL_SECONDS 60

/**
 * @def ACTIVE_TIME_SECONDS
 * @brief Tiempo activo en segundos para realizar mediciones múltiples de sensores
 * @details Durante este tiempo se toman lecturas periódicas de cada sensor según
 *          sus intervalos configurados (TEMP_INTERVAL, TDS_INTERVAL, etc.).
 * @note Debe ser suficiente para completar al menos una lectura de cada sensor.
 */
#define ACTIVE_TIME_SECONDS 30

/**
 * @def WIFI_CHECK_INTERVAL
 * @brief Número de ciclos de medición entre verificaciones WiFi
 * @details Sistema conecta WiFi cada WIFI_CHECK_INTERVAL lecturas almacenadas.
 *          Ejemplo: Si WIFI_CHECK_INTERVAL=2, conecta después de las lecturas #2, #4, #6, etc.
 * @note Valor bajo → Más frecuencia de transmisión, mayor consumo energético.
 *       Valor alto → Menos transmisiones, mayor acumulación de datos en RTC Memory.
 */
#define WIFI_CHECK_INTERVAL 2

/**
 * @def MANUAL_WAIT_TIMEOUT
 * @brief Timeout en milisegundos esperando solicitud del servidor en modo manual
 * @details Si no se recibe "request_all_data" del servidor en este tiempo, aborta.
 * @note 60000 ms = 1 minuto. Ajustar según latencia esperada del servidor.
 */
#define MANUAL_WAIT_TIMEOUT 30000

// ---Intervalos de muestreo para cada sensor (en milisegundos)---

/**
 * @def TEMP_INTERVAL
 * @brief Intervalo entre lecturas del sensor de temperatura en milisegundos
 * @note 10000 ms = 10 segundos. Durante ACTIVE_TIME_SECONDS (45s) se tomarán ~4 lecturas.
 */
#define TEMP_INTERVAL 5// Temperatura cada 5s

/**
 * @def TDS_INTERVAL
 * @brief Intervalo entre lecturas del sensor TDS en milisegundos
 * @note 10000 ms = 10 segundos. Durante ACTIVE_TIME_SECONDS (45s) se tomarán ~4 lecturas.
 */
#define TDS_INTERVAL 5 // TDS cada 5s

/**
 * @def TURBIDITY_INTERVAL
 * @brief Intervalo entre lecturas del sensor de turbidez en milisegundos
 * @note 10000 ms = 10 segundos. Durante ACTIVE_TIME_SECONDS (45s) se tomarán ~4 lecturas.
 */
#define TURBIDITY_INTERVAL 5 // Turbidez cada 5s

/**
 * @def PH_INTERVAL
 * @brief Intervalo entre lecturas del sensor de pH en milisegundos
 * @note 10000 ms = 10 segundos. Durante ACTIVE_TIME_SECONDS (45s) se tomarán ~4 lecturas.
 */
#define PH_INTERVAL 5 // pH cada 1s

// ——— Pines de Sensores ———

/**
 * @def TEMPERATURE_PIN
 * @brief Pin GPIO para sensor de temperatura DS18B20 (protocolo OneWire)
 * @note Requiere resistencia pull-up de 4.7kΩ a Vcc.
 */
#define TEMPERATURE_PIN 17

/**
 * @def TDS_PIN
 * @brief Pin GPIO (ADC) para sensor TDS analógico
 * @note Debe ser pin compatible con ADC1 del ESP32 (GPIO32-39).
 */
#define TDS_PIN 7

/**
 * @def TURBIDITY_PIN
 * @brief Pin GPIO (ADC) para sensor de turbidez analógico
 * @note Usa el pin predeterminado del módulo, válido para ADC1 en ESP32-S2.
 */
#define TURBIDITY_PIN TURBIDITY_SENSOR_PIN

/**
 * @def PH_PIN
 * @brief Pin GPIO (ADC) para sensor de pH analógico
 * @note Debe ser pin compatible con ADC1 del ESP32 (GPIO32-39).
 */
#define PH_PIN 1

/**
 * @def led
 * @brief Pin GPIO para LED indicador de estado
 * @note GPIO2 típicamente conectado a LED onboard en placas de desarrollo.
 */
#define led 2

// ——— Pines RTC

/**
 * @def RTC_SDA_PIN
 * @brief Pin GPIO para SDA del bus I2C del RTC MAX31328
 */
#define RTC_SDA_PIN 8

/**
 * @def RTC_SCL_PIN
 * @brief Pin GPIO para SCL del bus I2C del RTC MAX31328
 */
#define RTC_SCL_PIN 9

// ——— Configuración WiFi ———
const WiFiManager::wifi_config_t WIFI_CONFIG = {
    .ssid = "RED_MONITOREO",        ///< SSID de la red WiFi
    .password = "Holamundo6",       ///< Password de la red WiFi
    .server_ip = "192.168.137.1",   ///< IP del servidor WebSocket (hotspot móvil típic
    .server_port = 8765,            ///< Puerto del servidor WebSocket
    .connect_timeout_ms = 15000,    ///< Timeout conexión WiFi (15 segundos)
    .websocket_timeout_ms = 10000,  ///< Timeout conexión WebSocket (10 segundos)
    .max_retry_attempts = 3};       ///< Intentos de reconexión (no usado actualmente)

// ——— Instancias globales ———

/**
 * @var watchdog
 * @brief Instancia global del gestor de watchdog y monitoreo de salud
 * @note Parámetro true habilita salida por Serial.
 */
WatchdogManager watchdog(true);

/**
 * @var rtcMemory
 * @brief Instancia global del gestor de RTC Memory persistente
 * @note Parámetro true habilita salida por Serial.
 */
RTCMemoryManager rtcMemory(true);

/**
 * @var deepSleep
 * @brief Instancia global del gestor de deep sleep
 * @note Parámetros: sleep_seconds=80, active_seconds=45, serial_enable=true.
 */
DeepSleepManager deepSleep(SLEEP_INTERVAL_SECONDS, ACTIVE_TIME_SECONDS, true);

/**
 * @var wifiManager
 * @brief Instancia global del gestor de WiFi y WebSocket
 * @note Parámetro true habilita salida por Serial.
 */
WiFiManager wifiManager(true);

/**
 * @var rtcExterno
 * @brief Instancia global del RTC externo MAX31328
 * @note Proporciona timestamps Unix precisos y sincronización NTP.
 */
MAX31328RTC rtcExterno;
/**
 * @var calibManager
 * @brief Instancia global del gestor de calibración de sensores
 * @note Parámetro true habilita salida por Serial.
 */
CalibrationManager calibManager(true);

// ——— Variables para tracking de última lectura de cada sensor ———
// >>> Estas son las líneas que se añadieron (última vez de lectura)

/**
 * @var lastTempRead
 * @brief Timestamp (millis()) de la última lectura de temperatura
 * @note Usado para control de intervalo no bloqueante en loop de medición.
 */
unsigned long lastTempRead = 0; // >>> Esta es la línea que se añadió

/**
 * @var lastTDSRead
 * @brief Timestamp (millis()) de la última lectura de TDS
 * @note Usado para control de intervalo no bloqueante en loop de medición.
 */
unsigned long lastTDSRead = 0; // >>> Esta es la línea que se añadió

/**
 * @var lastTurbidityRead
 * @brief Timestamp (millis()) de la última lectura de turbidez
 * @note Usado para control de intervalo no bloqueante en loop de medición.
 */
unsigned long lastTurbidityRead = 0; // >>> Esta es la línea que se añadió

/**
 * @var lastPHRead
 * @brief Timestamp (millis()) de la última lectura de pH
 * @note Usado para control de intervalo no bloqueante en loop de medición.
 */
unsigned long lastPHRead = 0; // >>> Esta es la línea que se añadió

/**
 * @var forceManualCheck
 * @brief Bandera para forzar verificación WiFi fuera de programación normal
 * @note Se activa al detectar despertar por botón (ESP_SLEEP_WAKEUP_EXT0).
 */
bool forceManualCheck = false;

// ——— Estructuras globales para almacenar últimas lecturas ———

/**
 * @var tempReading
 * @brief Última lectura del sensor de temperatura
 */
TemperatureReading tempReading;

/**
 * @var tdsReading
 * @brief Última lectura del sensor TDS
 */
TDSReading tdsReading;

/**
 * @var turbidityReading
 * @brief Última lectura del sensor de turbidez
 */
TurbidityReading turbidityReading;

/**
 * @var phReading
 * @brief Última lectura del sensor de pH
 */
pHReading phReading;

/**
 * @brief Función setup() - Punto de entrada del programa después de boot/wake
 * @details Secuencia completa de inicialización y operación:
 *          1. Inicializa Serial y LED indicador
 *          2. Inicializa y alimenta watchdog
 *          3. Valida integridad de RTC Memory
 *          4. Inicializa RTC externo MAX31328
 *          5. Realiza health check del sistema
 *          6. Inicializa todos los sensores (temperatura, TDS, turbidez, pH)
 *          7. Loop de medición no bloqueante durante ACTIVE_TIME_SECONDS
 *          8. Obtiene timestamp del RTC externo
 *          9. Almacena lecturas en RTC Memory
 *          10. Verifica si corresponde conexión WiFi
 *          11. Si corresponde, conecta y espera solicitud del servidor
 *          12. Sincroniza RTC con NTP si necesario
 *          13. Muestra resumen del ciclo y estadísticas
 *          14. Entra en deep sleep hasta próximo ciclo
 *
 * @note Esta función se ejecuta después de cada despertar (deep sleep wake, reset, power on).
 * @note Todo el código crítico debe completarse antes de entrar en deep sleep.
 */
void setup()
{
    Serial.begin(115200);
    delay(100);

    Serial.println("\n=== SISTEMA DE MONITOREO DE CALIDAD DEL AGUA ===");
    Serial.println("================================================\n");

    pinMode(led, OUTPUT);
    digitalWrite(led, HIGH);
    // ——— 1. INICIALIZAR WATCHDOG ———
    watchdog.begin();
    watchdog.feedWatchdog();

    // 1.5. INICIALIZAR CALIBRATION MANAGER
    calibManager.begin();
    watchdog.feedWatchdog();

    // ——— 2. VERIFICAR/INICIALIZAR RTC MEMORY ———
    rtcMemory.begin();
    if (!rtcMemory.validateIntegrity())
    {
        // Serial.println(" Datos RTC Memory corruptos - Inicializando");
        rtcMemory.initialize();
        watchdog.logError(WatchdogManager::ERROR_RTC_CORRUPTION,
                            WatchdogManager::SEVERITY_WARNING, 0);
    }
    else
    {
        Serial.println(" Datos RTC Memory válidos");

        // Mostrar información del sistema
        deepSleep.begin();
        deepSleep.printWakeupReason();
        watchdog.displaySystemHealth();

        if (watchdog.getHealthScore() < 20)
        {
            Serial.println("Salud muy baja - Intentando recovery");
            watchdog.attemptRecovery();
        }
    }

    watchdog.feedWatchdog();

    // ——— 3. INICIALIZAR RTC EXTERNO MAX31328 ———
    Serial.println("\n === INICIALIZANDO RTC MAX31328 ===");
    bool rtcAvailable = false;

    // Serial.printf("Inicializando MAX31328 (SDA=%d, SCL=%d)...\n", RTC_SDA_PIN, RTC_SCL_PIN);

    if (!rtcExterno.begin(RTC_SDA_PIN, RTC_SCL_PIN))
    {
        Serial.println(" Error inicializando RTC MAX31328");

        watchdog.logError(WatchdogManager::ERROR_SENSOR_INIT_FAIL,
                            WatchdogManager::SEVERITY_WARNING, 0x31328);
    }
    else
    {
        Serial.println(" RTC MAX31328 inicializado correctamente");
        rtcAvailable = true;

        rtcExterno.printDebugInfo();
        if (deepSleep.isFirstBoot() || rtcExterno.hasLostTime())
        {
            Serial.println(" RTC necesita sincronización");
        }

        watchdog.recordSuccess();
    }

    watchdog.feedWatchdog();

    // ——— 4. HEALTH CHECK ———
    bool health_ok = watchdog.performHealthCheck();
    if (!health_ok && watchdog.getConsecutiveFailures() >= 5)
    {
        Serial.println(" Sistema en falla crítica");
        watchdog.attemptRecovery();
    }

    // ——— 5. CONFIGURAR ERROR LOGGER PARA SENSORES ———
    auto errorLogger = [](int code, int severity, uint32_t context)
    {
        watchdog.logError(static_cast<WatchdogManager::error_code_t>(code),
                            static_cast<WatchdogManager::error_severity_t>(severity),
                            context);
    };

    // ——— 6. INICIALIZAR SENSOR DE TEMPERATURA ———
    // Serial.println("\n Inicializando sensor de temperatura...");
    TemperatureSensor::setErrorLogger(errorLogger);

    if (!TemperatureSensor::initialize(TEMPERATURE_PIN))
    {
        Serial.println(" Error inicializando sensor temperatura");
        watchdog.logError(WatchdogManager::ERROR_SENSOR_INIT_FAIL,
                            WatchdogManager::SEVERITY_CRITICAL, TEMPERATURE_PIN);
        watchdog.recordFailure();
    }
    else
    {
        Serial.println(" Sensor temperatura inicializado");
        watchdog.recordSuccess();
    }

    watchdog.feedWatchdog();

    // ——— 7. INICIALIZAR SENSOR TDS ———
    // Serial.println("\n Inicializando sensor TDS...");
    TDSSensor::setErrorLogger(errorLogger);

    if (!TDSSensor::initialize(TDS_PIN))
    {
        Serial.println(" Error inicializando sensor TDS");
        watchdog.logError(WatchdogManager::ERROR_SENSOR_INIT_FAIL,
                            WatchdogManager::SEVERITY_CRITICAL, TDS_PIN);
        watchdog.recordFailure();
    }
    else
    {
        Serial.println(" Sensor TDS inicializado");

        float kValue, vOffset;
        TDSSensor::getCalibration(kValue, vOffset);

        watchdog.recordSuccess();
    }

    watchdog.feedWatchdog();

    // ——— 8. INICIALIZAR SENSOR DE TURBIDEZ ———
    // Serial.println("\n Inicializando sensor de turbidez...");
    TurbiditySensor::setErrorLogger(errorLogger);

    if (!TurbiditySensor::initialize(TURBIDITY_PIN))
    {
        Serial.println(" Error inicializando sensor turbidez");
        watchdog.logError(WatchdogManager::ERROR_SENSOR_INIT_FAIL,
                            WatchdogManager::SEVERITY_CRITICAL, TURBIDITY_PIN);
        watchdog.recordFailure();
    }
    else
    {
        Serial.println(" Sensor turbidez inicializado");
        watchdog.recordSuccess();
    }

    watchdog.feedWatchdog();

    // ——— 9. INICIALIZAR SENSOR DE pH ———
    // Serial.println("\n Inicializando sensor pH...");
    pHSensor::setErrorLogger(errorLogger);

    if (!pHSensor::initialize(PH_PIN))
    {
        Serial.println(" Error inicializando sensor pH");
        watchdog.logError(WatchdogManager::ERROR_SENSOR_INIT_FAIL,
                            WatchdogManager::SEVERITY_CRITICAL, PH_PIN);
        watchdog.recordFailure();
    }
    else
    {
        Serial.println(" Sensor pH inicializado");

        float phOffset, phSlope;
        pHSensor::getCalibration(phOffset, phSlope);

        watchdog.recordSuccess();
    }

    watchdog.feedWatchdog();

    // ——— 10. TOMAR LECTURAS DE SENSORES ———
    Serial.println("\n === TOMANDO LECTURAS DE SENSORES ===");

    unsigned long startActive = millis(); // >>> Esta es la línea que se añadió

    while ((millis() - startActive) < (ACTIVE_TIME_SECONDS * 1000))
    {                                           // >>> Esta es la línea que se añadió
        unsigned long currentMillis = millis(); // >>> Esta es la línea que se añadió

    // Serial.println(" Leyendo temperatura...");
        if (currentMillis - lastTempRead >= TEMP_INTERVAL)
        {                                                              // >>> Esta es la línea que se añadió
            tempReading = TemperatureSensor::takeReadingWithTimeout(); // >>> Esta es la línea que se añadió
            if (tempReading.valid)
            {
                Serial.printf("Temperatura: %.2f °C\n", tempReading.temperature); // >>> Esta es la línea que se añadió
            }
            lastTempRead = currentMillis; // >>> Esta es la línea que se añadió
        }

    watchdog.feedWatchdog();

    // Serial.println(" Leyendo TDS...");
        if (currentMillis - lastTDSRead >= TDS_INTERVAL)
        {                                                         // >>> Esta es la línea que se añadió
            tdsReading = TDSSensor::takeReadingWithTimeout(25.0); // >>> Esta es la línea que se añadió
            if (tdsReading.valid)
            {
                Serial.printf("TDS: %.1f ppm | EC: %.1f µS/cm\n", tdsReading.tds_value, tdsReading.ec_value); // >>> Esta es la línea que se añadió
            }
            lastTDSRead = currentMillis; // >>> Esta es la línea que se añadió
        }
    watchdog.feedWatchdog();

    // Serial.println(" Leyendo turbidez...");
        if (currentMillis - lastTurbidityRead >= TURBIDITY_INTERVAL)
        {                                                                 // >>> Esta es la línea que se añadió
            turbidityReading = TurbiditySensor::takeReadingWithTimeout(); // >>> Esta es la línea que se añadió
            if (turbidityReading.valid)
            {
                Serial.printf("Turbidez: %.1f NTU\n", turbidityReading.turbidity_ntu); // >>> Esta es la línea que se añadió
            }
            lastTurbidityRead = currentMillis; // >>> Esta es la línea que se añadió
        }
    watchdog.feedWatchdog();

    // Serial.println(" Leyendo pH...");
        if (currentMillis - lastPHRead >= PH_INTERVAL)
        {                                                       // >>> Esta es la línea que se añadió
            phReading = pHSensor::takeReadingWithTimeout(25.0); // >>> Esta es la línea que se añadió
            if (phReading.valid)
            {
                Serial.printf("pH: %.2f\n", phReading.ph_value); // >>> Esta es la línea que se añadió
            }
            lastPHRead = currentMillis; // >>> Esta es la línea que se añadió
        }

        delay(50); // >>> Esta es la línea que se añadió (evita saturar CPU)
    }
    watchdog.feedWatchdog();

    // ——— 11. OBTENER TIMESTAMP DEL RTC MAX31328 ———
    uint32_t rtcTimestamp = 0;
    String rtcDateTime = "No disponible";

    if (rtcAvailable && rtcExterno.isPresent())
    {
        rtcTimestamp = rtcExterno.getUnixTimestamp();
        rtcDateTime = rtcExterno.getFormattedDateTime();

        if (rtcTimestamp < 1609459200)
        {
            Serial.println(" Timestamp RTC inválido - usando tiempo relativo");
            rtcTimestamp = millis() / 1000; // Segundos desde boot
        }
        else
        {
            Serial.printf(" Timestamp RTC: %u (%s)\n", rtcTimestamp, rtcDateTime.c_str());
        }
    }
    else
    {
        Serial.println(" RTC no disponible - usando timestamp relativo");
        rtcTimestamp = millis() / 1000;
    }

    // ——— 12. ALMACENAR EN RTC MEMORY ———
    bool readingStored = false;

    if (tempReading.valid || tdsReading.valid || turbidityReading.valid || phReading.valid)
    {

        RTCMemoryManager::SensorReading reading = rtcMemory.createFullReading(
            tempReading.valid ? tempReading.temperature : 0.0f,
            phReading.valid ? phReading.ph_value : 0.0f,
            turbidityReading.valid ? turbidityReading.turbidity_ntu : 0.0f,
            tdsReading.valid ? tdsReading.tds_value : 0.0f,
            tdsReading.valid ? tdsReading.ec_value : 0.0f,
            (tempReading.sensor_status << 0) |
                (tdsReading.sensor_status << 2) |
                (turbidityReading.sensor_status << 4) |
                (phReading.sensor_status << 6));

        reading.rtc_timestamp = rtcTimestamp;

        reading.valid = (tempReading.valid || tdsReading.valid || turbidityReading.valid || phReading.valid);

        if (rtcMemory.storeReading(reading))
        {
            Serial.println("\n === LECTURA ALMACENADA ===");
            Serial.printf(" Lectura #%d guardada exitosamente\n", rtcMemory.getTotalReadings());
            Serial.printf(" Timestamp: %s (Unix: %u)\n", rtcDateTime.c_str(), rtcTimestamp);

            if (tempReading.valid)
            {
                Serial.printf(" Temperatura: %.2f°C\n", tempReading.temperature);
            }
            if (tdsReading.valid)
            {
                Serial.printf(" TDS: %.1f ppm (EC: %.1f µS/cm)\n",
                                tdsReading.tds_value, tdsReading.ec_value);
            }
            if (turbidityReading.valid)
            {
                Serial.printf(" Turbidez: %.1f NTU (%s)\n",
                                turbidityReading.turbidity_ntu,
                                TurbiditySensor::getWaterQuality(turbidityReading.turbidity_ntu).c_str());
            }
            if (phReading.valid)
            {
                Serial.printf(" pH: %.2f (%s)\n",
                                phReading.ph_value,
                                pHSensor::getWaterType(phReading.ph_value).c_str());
            }
            Serial.println("==========================");

            watchdog.recordSuccess();
            readingStored = true;
        }
        else
        {
            Serial.println(" Error almacenando lecturas");
            watchdog.logError(WatchdogManager::ERROR_RTC_WRITE_FAIL,
                                WatchdogManager::SEVERITY_CRITICAL, 0);
            watchdog.recordFailure();
        }
    }
    else
    {
        Serial.println(" Todas las lecturas inválidas - no se almacena");
        watchdog.recordFailure();
    }

    // ——— 13. VERIFICAR SI ES MOMENTO DE CONECTAR WIFI ———
    bool shouldCheckWiFi = (rtcMemory.getTotalReadings() % WIFI_CHECK_INTERVAL == 0) && (rtcMemory.getTotalReadings() > 0);

    if (deepSleep.getWakeupCause() == ESP_SLEEP_WAKEUP_EXT0)
    {
        Serial.println(" Despertar por botón - Forzando verificación WiFi");
        forceManualCheck = true;
    }

    if (shouldCheckWiFi || forceManualCheck)
    {
        Serial.println("\n === VERIFICACIÓN WIFI PROGRAMADA ===");
        Serial.printf(" Datos almacenados: %d lecturas\n", rtcMemory.getTotalReadings());
        // Serial.println(" Conectando para verificar si hay solicitud de descarga...");

        wifiManager.begin(WIFI_CONFIG);
        wifiManager.setManagers(&rtcMemory, &watchdog);
        wifiManager.setManualMode(true);

        wifiManager.setErrorCallback([](WatchdogManager::error_code_t code,
                                        WatchdogManager::error_severity_t severity,
                                        uint32_t context)
                                        { watchdog.logError(code, severity, context); });

        watchdog.feedWatchdog();

        bool wifiSuccess = wifiManager.transmitDataManual(120, MANUAL_WAIT_TIMEOUT);

        if (wifiSuccess)
        {
            Serial.println(" Proceso WiFi completado");

            if (rtcAvailable && wifiManager.isWiFiConnected())
            {
                if (rtcExterno.hasLostTime() || deepSleep.isFirstBoot())
                {
                    Serial.println("\n Sincronizando RTC con servidor NTP...");
                    if (rtcExterno.syncWithNTP("co.pool.ntp.org", -5))
                    {
                        Serial.println(" RTC sincronizado correctamente con NTP");
                        Serial.printf(" Nueva fecha/hora: %s\n", rtcExterno.getFormattedDateTime().c_str());
                    }
                    else
                    {
                        Serial.println(" No se pudo sincronizar RTC con NTP");
                    }
                }
            }

            String stats = wifiManager.getTransmissionStats();
            if (stats.indexOf("Datos enviados: 0") == -1)
            {
                Serial.println(" Datos descargados exitosamente por el usuario");
            }
            else
            {
                Serial.println(" No hubo solicitud de descarga");
            }

            watchdog.recordSuccess();
        }
        else
        {
            Serial.println(" Falló conexión WiFi");
            watchdog.logError(WatchdogManager::ERROR_WIFI_FAIL,
                                WatchdogManager::SEVERITY_WARNING, 0);
            watchdog.recordFailure();
        }

        // Mostrar estadísticas
        Serial.println(wifiManager.getTransmissionStats());

        watchdog.feedWatchdog();
        forceManualCheck = false;
    }
    else
    {
        Serial.printf(" Lecturas: %d/%d (WiFi check en %d lecturas)\n",
                        rtcMemory.getTotalReadings() % WIFI_CHECK_INTERVAL,
                        WIFI_CHECK_INTERVAL,
                        WIFI_CHECK_INTERVAL - (rtcMemory.getTotalReadings() % WIFI_CHECK_INTERVAL));
        Serial.println(" Sin verificación WiFi programada");
    }

    // ——— 14. MOSTRAR DATOS Y ERRORES ———
    rtcMemory.displayStoredReadings(5);
    watchdog.displayErrorLog(3);

    // ——— 15. VERIFICAR EMERGENCIA ———
    if (watchdog.getConsecutiveFailures() >= 10)
    {
        Serial.println(" DEMASIADOS FALLOS - MODO EMERGENCIA");
        watchdog.handleEmergency();
        deepSleep.goToSleepFor(300, true);
    }

    watchdog.feedWatchdog();

    // ——— 16. RESUMEN FINAL ———
    Serial.println("\n === RESUMEN DEL CICLO ===");

    Serial.println(" Lecturas de sensores:");
    if (tempReading.valid)
    {
        Serial.printf("    Temperatura: %.2f°C (VÁLIDA)\n", tempReading.temperature);
    }
    else
    {
        Serial.println("    Temperatura: --- (INVÁLIDA)");
    }

    if (tdsReading.valid)
    {
        Serial.printf("    TDS: %.1f ppm | EC: %.1f µS/cm (VÁLIDA)\n",
                        tdsReading.tds_value, tdsReading.ec_value);
    }
    else
    {
        Serial.println("    TDS: --- ppm (INVÁLIDA)");
    }

    if (turbidityReading.valid)
    {
        Serial.printf("    Turbidez: %.1f NTU | %s (VÁLIDA)\n",
                        turbidityReading.turbidity_ntu,
                        TurbiditySensor::getWaterQuality(turbidityReading.turbidity_ntu).c_str());
    }
    else
    {
        Serial.println("    Turbidez: --- NTU (INVÁLIDA)");
    }

    if (phReading.valid)
    {
        Serial.printf("    pH: %.2f | %s (VÁLIDA)\n",
                        phReading.ph_value,
                        pHSensor::getWaterType(phReading.ph_value).c_str());
    }
    else
    {
        Serial.println("    pH: -.-- (INVÁLIDA)");
    }

    Serial.println("\n === ESTADO RTC MAX31328 ===");
    if (rtcAvailable && rtcExterno.isPresent())
    {
        Serial.printf("Hora actual: %s\n", rtcExterno.getFormattedDateTime().c_str());
        Serial.printf("Unix timestamp: %u\n", rtcExterno.getUnixTimestamp());
        Serial.printf("Funcionando: %s\n", rtcExterno.isRunning() ? "Sí" : "No");

        if (rtcExterno.hasLostTime())
        {
            Serial.println(" RTC perdió la hora - Se sincronizará en próxima conexión WiFi");
        }
    }
    else
    {
        Serial.println(" RTC no disponible - usando timestamps relativos");
    }
    Serial.println("==========================");

    Serial.printf("\n Total lecturas almacenadas: %d\n", rtcMemory.getTotalReadings());
    Serial.printf(" Salud sistema: %d%%\n", watchdog.getHealthScore());
    Serial.printf(" Fallos consecutivos: %d\n", watchdog.getConsecutiveFailures());
    Serial.printf(" Próximo check WiFi en: %d lecturas\n",
                    WIFI_CHECK_INTERVAL - (rtcMemory.getTotalReadings() % WIFI_CHECK_INTERVAL));

    uint64_t totalCycle, activeTime, sleepTime;
    deepSleep.getCycleInfo(totalCycle, activeTime, sleepTime);
    Serial.printf(" Duty Cycle: %.1f%% (%llu/%llu seg)\n",
                  (activeTime * 100.0) / totalCycle, activeTime, totalCycle);

    Serial.println("============================");

    // ——— 17. ENTRAR EN DEEP SLEEP ———
    Serial.printf("\n Entrando en Deep Sleep por %llu segundos\n",
                    deepSleep.calculateSleepTime());
    Serial.printf(" Próximo despertar en %.1f minutos\n",
                    deepSleep.calculateSleepTime() / 60.0);
    Serial.println("==========================================\n");

    delay(500);
    deepSleep.goToSleep(true);
}

void loop()
{
    // No se ejecuta con Deep Sleep
    Serial.println(" ERROR: No entró en Deep Sleep");
    delay(5000);
    ESP.restart();
}