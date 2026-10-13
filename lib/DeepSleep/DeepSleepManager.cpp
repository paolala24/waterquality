/**
 * @file DeepSleepManager.cpp
 * @brief Implementación de DeepSleepManager para gestión del modo Deep Sleep en ESP32.
 *
 * Este fichero contiene la implementación de la clase DeepSleepManager, que encapsula
 * las funciones necesarias para configurar y activar el modo Deep Sleep del ESP32,
 * consultar la causa de wakeup, configurar wakeup por temporizador o GPIO, y generar
 * logs mediante un callback o Serial.
 *
 * @version 1.0
 * @date 2025-10-01
 * @author Daniel Acosta - Santiago Erazo
 */

#include "DeepSleepManager.h"
#include <stdarg.h>
/**
 * @brief Constructor de la clase DeepSleepManager.
 * @param sleepInterval Intervalo total de ciclo (segundos).
 * @param activeTime Tiempo activo dentro del ciclo (segundos).
 * @param enableSerial Habilita o deshabilita la salida por Serial.
 */

// Constructor
DeepSleepManager::DeepSleepManager(uint64_t sleepInterval, uint64_t activeTime, bool enableSerial) 
    : _sleepInterval(sleepInterval), _activeTime(activeTime), _enableSerialOutput(enableSerial), _logCallback(nullptr) {
}

/**
 * @brief Inicializa el DeepSleepManager, habilitando Serial si se requiere.
 */

// Inicialización
void DeepSleepManager::begin() {
    if (_enableSerialOutput && !Serial) {
        Serial.begin(115200); ///< Inicializa la comunicación Serial a 115200 baudios
        delay(100); ///< Espera breve para asegurar el arranque del puerto
    }
    
/**
 * @brief Configura un nuevo intervalo de ciclo (activo + sleep).
 * @param seconds Duración total en segundos.
 */

    //log("=== Deep Sleep Manager Inicializado ===");
    //logf("Intervalo total: %llu segundos", _sleepInterval);
    //logf("Tiempo activo: %llu segundos", _activeTime);
    //logf("Tiempo de sleep: %llu segundos", calculateSleepTime());
}

// Configurar intervalo de sleep
void DeepSleepManager::setSleepInterval(uint64_t seconds) {
    _sleepInterval = seconds;
    //logf("Intervalo actualizado: %llu segundos", _sleepInterval);
}

/**
 * @brief Configura el tiempo activo dentro del ciclo.
 * @param seconds Tiempo en segundos.
 */

// Configurar tiempo activo
void DeepSleepManager::setActiveTime(uint64_t seconds) {
    _activeTime = seconds;
    //logf("Tiempo activo actualizado: %llu segundos", _activeTime);
}

/**
 * @brief Obtiene la causa del despertar en formato legible (string).
 * @return Cadena con la causa del despertar.
 */

// Obtener razón del despertar como string
String DeepSleepManager::getWakeupReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return "Timer RTC";
        case ESP_SLEEP_WAKEUP_EXT0:
            return "Señal externa RTC_IO";
        case ESP_SLEEP_WAKEUP_EXT1:
            return "Señal externa RTC_CNTL";
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            return "Touchpad";
        case ESP_SLEEP_WAKEUP_ULP:
            return "ULP program";
        case ESP_SLEEP_WAKEUP_GPIO:
            return "GPIO";
        case ESP_SLEEP_WAKEUP_UART:
            return "UART";
        default:
            return "Arranque normal/reset";
    }
}

/**
 * @brief Devuelve la causa de despertar en formato enum.
 * @return esp_sleep_wakeup_cause_t con la causa del despertar.
 */

// Obtener causa del despertar como enum
esp_sleep_wakeup_cause_t DeepSleepManager::getWakeupCause() {
    return esp_sleep_get_wakeup_cause();
}

/**
 * @brief Imprime por log/serial la causa del despertar.
 */

// Imprimir razón del despertar
void DeepSleepManager::printWakeupReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            log(" Desperté por temporizador RTC");
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            log(" Desperté por señal externa RTC_IO");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            log(" Desperté por señal externa RTC_CNTL");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            log(" Desperté por touchpad");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            log(" Desperté por programa ULP");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            log(" Desperté por GPIO");
            break;
        case ESP_SLEEP_WAKEUP_UART:
            log(" Desperté por UART");
            break;
        default:
            log(" Arranque normal (reset/programación)");
            break;
    }
}

/**
 * @brief Habilita el despertar por temporizador (RTC).
 * @param seconds Segundos hasta el próximo despertar.
 */

// Habilitar despertar por temporizador
void DeepSleepManager::enableTimerWakeup(uint64_t seconds) {
    uint64_t sleepTime = (seconds == 0) ? calculateSleepTime() : seconds;
    esp_sleep_enable_timer_wakeup(sleepTime * US_TO_S_FACTOR);
    logf(" Timer wakeup configurado: %llu segundos", sleepTime);
}

/**
 * @brief Habilita el despertar por un pin externo.
 * @param pin Número de GPIO.
 * @param level Nivel lógico que provoca el despertar.
 */

// Habilitar despertar por pin externo
void DeepSleepManager::enableExternalWakeup(int pin, int level) {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level);
    logf(" External wakeup configurado: GPIO%d, nivel %d", pin, level);
}

/**
 * @brief Entra en modo Deep Sleep usando el intervalo configurado.
 * @param showCountdown Muestra por log/serial el tiempo antes de dormir.
 */

// Entrar en Deep Sleep
void DeepSleepManager::goToSleep(bool showCountdown) {
    uint64_t sleepTime = calculateSleepTime();
    
    // Configurar timer wakeup
    esp_sleep_enable_timer_wakeup(sleepTime * US_TO_S_FACTOR);
    
    if (showCountdown) {
        logf(" Entrando en Deep Sleep por %llu segundos...", sleepTime);
        logf("Ciclo: %llu min total (%llu min activo + %llu min sleep)", 
            _sleepInterval/60, _activeTime/60, sleepTime/60);
        
        log("==========================================");
        delay(100);  // Dar tiempo para que se envíe el mensaje
    }
    
    // Entrar en Deep Sleep
    esp_deep_sleep_start(); ///< Inicia el modo Deep Sleep
}

/**
 * @brief Entra en Deep Sleep por un tiempo específico.
 * @param seconds Duración en segundos.
 * @param showCountdown Mostrar información antes de dormir.
 */

// Entrar en Deep Sleep por tiempo específico
void DeepSleepManager::goToSleepFor(uint64_t seconds, bool showCountdown) {
    esp_sleep_enable_timer_wakeup(seconds * US_TO_S_FACTOR);
    
    if (showCountdown) {
        logf(" Entrando en Deep Sleep por %llu segundos...", seconds);
        delay(100);
    }
    
    esp_deep_sleep_start();
}

/**
 * @brief Calcula el tiempo de sleep restante en el ciclo.
 * @return Tiempo en segundos.
 */

// Calcular tiempo de sleep restante
uint64_t DeepSleepManager::calculateSleepTime() {
    if (_sleepInterval <= _activeTime) {
        log(" Warning: Tiempo activo >= intervalo total");
        return 10;  // Mínimo 10 segundos de sleep de seguridad
    }
    return _sleepInterval - _activeTime;
}

 /**
 * @brief Obtiene información del ciclo (total, activo y sleep).
 * @param totalCycle Referencia donde se almacena el ciclo total.
 * @param activeTime Referencia donde se almacena el tiempo activo.
 * @param sleepTime Referencia donde se almacena el tiempo de sleep.
 */

// Obtener información del ciclo
void DeepSleepManager::getCycleInfo(uint64_t &totalCycle, uint64_t &activeTime, uint64_t &sleepTime) {
    totalCycle = _sleepInterval;
    activeTime = _activeTime;
    sleepTime = calculateSleepTime();
}

/**
 * @brief Verifica si es el primer arranque (sin causa de despertar previa).
 * @return true si es primer arranque, false en caso contrario.
 */

// Verificar si es primera ejecución
bool DeepSleepManager::isFirstBoot() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED;
}

/**
 * @brief Habilita o deshabilita la salida por Serial.
 * @param enable true para habilitar, false para deshabilitar.
 */

// Habilitar/deshabilitar Serial
void DeepSleepManager::enableSerial(bool enable) {
    _enableSerialOutput = enable;
}

/**
 * @brief Configura un callback externo para logging.
 * @param callback Puntero a función de tipo LogCallback.
 */

// Configurar callback de logging
void DeepSleepManager::setLogCallback(LogCallback callback) {
    _logCallback = callback;
}

/**
 * @brief Inicia un modo de sleep de emergencia con tiempo reducido.
 * @param emergencySeconds Duración del sleep de emergencia en segundos.
 */

// Sleep de emergencia
void DeepSleepManager::emergencySleep(uint64_t emergencySeconds) {
    log(" MODO EMERGENCIA - Sleep reducido");
    logf("Durmiendo %llu segundos...", emergencySeconds);
    
    esp_sleep_enable_timer_wakeup(emergencySeconds * US_TO_S_FACTOR);
    delay(1000);
    esp_deep_sleep_start();
}

/**
 * @brief Devuelve una cadena con el estado actual del gestor de Deep Sleep.
 * @return Cadena con la información de estado.
 */

// Obtener estado actual
String DeepSleepManager::getStatus() {
    String status = "=== Deep Sleep Manager Status ===\n";
    status += "Intervalo total: " + String(_sleepInterval) + "s (" + String(_sleepInterval/60) + "min)\n";
    status += "Tiempo activo: " + String(_activeTime) + "s (" + String(_activeTime/60) + "min)\n";
    status += "Tiempo sleep: " + String(calculateSleepTime()) + "s (" + String(calculateSleepTime()/60) + "min)\n";
    status += "Duty cycle: " + String((_activeTime * 100.0) / _sleepInterval, 1) + "%\n";
    status += "Última causa despertar: " + getWakeupReason() + "\n";
    status += "Primera ejecución: " + String(isFirstBoot() ? "Sí" : "No") + "\n";
    status += "Serial habilitado: " + String(_enableSerialOutput ? "Sí" : "No") + "\n";
    status += "================================";
    
    return status;
}

/**
 * @brief Log simple de mensajes.
 * @param message Cadena a imprimir o enviar a callback.
 *
 * Si se ha registrado un callback con setLogCallback(), se invoca; en caso contrario
 * se imprime por Serial si está habilitado.
 */

// Métodos privados de logging
void DeepSleepManager::log(const char* message) {
    if (_logCallback) {
        _logCallback(message);
    } else if (_enableSerialOutput && Serial) {
        Serial.println(message);
    }
}

/**
 * @brief Log con formato (tipo printf).
 * @param format Cadena de formato.
 * @param ... Argumentos variables.
 *
 * Construye un buffer interno (256 bytes) y delega el resultado a log().
 */

void DeepSleepManager::logf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    log(buffer);
}