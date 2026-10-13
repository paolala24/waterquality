/**
 * @file DeepSleepManager.h
 * @brief Definición de la clase DeepSleepManager para gestión del modo Deep Sleep en ESP32.
 * 
 * Esta clase permite configurar, controlar y monitorear el modo Deep Sleep del ESP32.
 * Incluye funciones para definir intervalos de sueño, tiempo activo, causas de despertar,
 * configuración de wakeup por temporizador o GPIO, y generación de logs.
 * 
 * @version 1.0
 * @date 2025-10-01
 * @author Daniel Acosta - Santiago Erazo
 */

#ifndef DEEPSLEEP_MANAGER_H
#define DEEPSLEEP_MANAGER_H

#include <Arduino.h>
#include "esp_sleep.h"

/**
 * @class DeepSleepManager
 * @brief Clase para manejo de Deep Sleep en ESP32
 * 
 * Esta clase encapsula todas las funcionalidades relacionadas con 
 * el modo Deep Sleep del ESP32, incluyendo configuración de temporizador,
 * razones de despertar y gestión de energía.
 */
class DeepSleepManager {
private:
    static const uint64_t US_TO_S_FACTOR = 1000000ULL;  ///< Factor de conversión µs → s
    
    uint64_t _sleepInterval;        ///< Intervalo de sleep en segundos
    uint64_t _activeTime;           ///< Tiempo activo por ciclo en segundos
    bool _enableSerialOutput;       ///< Habilitar salida por Serial
    
/**
 * @brief Definición de tipo para callback de logging.
 * 
 * El callback permite redirigir los mensajes de log a una función
 * definida por el usuario.
 */       
    typedef void (*LogCallback)(const char* message);
    LogCallback _logCallback;

public:
    /**
     * @brief Constructor de la clase DeepSleepManager
     * @param sleepInterval Intervalo total de ciclo en segundos (default: 1200 = 20 min)
     * @param activeTime Tiempo activo por ciclo en segundos (default: 60)
     * @param enableSerial Habilitar mensajes por Serial (default: true)
     */
    DeepSleepManager(uint64_t sleepInterval = 1200, uint64_t activeTime = 60, bool enableSerial = true);
    
    /**
     * @brief Inicializar el gestor de Deep Sleep
     */
    void begin();
    
    /**
     * @brief Configurar intervalo de sleep
     * @param seconds Segundos totales del ciclo
     */
    void setSleepInterval(uint64_t seconds);
    
    /**
     * @brief Configurar tiempo activo por ciclo
     * @param seconds Segundos activos por ciclo
     */
    void setActiveTime(uint64_t seconds);
    
    /**
     * @brief Obtener la razón del último despertar
     * @return Causa del despertar como string descriptivo
     */
    String getWakeupReason();
    
    /**
     * @brief Obtener la razón del despertar como enum
     * @return esp_sleep_wakeup_cause_t
     */
    esp_sleep_wakeup_cause_t getWakeupCause();
    
    /**
     * @brief Imprimir la razón del despertar por Serial
     */
    void printWakeupReason();
    
    /**
     * @brief Configurar despertar por temporizador RTC
     * @param seconds Segundos hasta despertar (si 0, usa configuración interna)
     */
    void enableTimerWakeup(uint64_t seconds = 0);
    
    /**
     * @brief Configurar despertar por pin externo (RTC_IO)
     * @param pin Pin GPIO para despertar
     * @param level Nivel lógico que despierta (0 o 1)
     */
    void enableExternalWakeup(int pin, int level);
    
    /**
     * @brief Entrar en modo Deep Sleep
     * @param showCountdown Mostrar cuenta regresiva antes de dormir
     */
    void goToSleep(bool showCountdown = true);
    
    /**
     * @brief Entrar en Deep Sleep por tiempo específico
     * @param seconds Segundos a dormir
     * @param showCountdown Mostrar cuenta regresiva
     */
    void goToSleepFor(uint64_t seconds, bool showCountdown = true);
    
    /**
     * @brief Calcular tiempo de sleep restante basado en tiempo activo
     * @return Segundos a dormir
     */
    uint64_t calculateSleepTime();
    
    /**
     * @brief Obtener información del ciclo actual
     * @param totalCycle Referencia para almacenar tiempo total del ciclo
     * @param activeTime Referencia para almacenar tiempo activo
     * @param sleepTime Referencia para almacenar tiempo de sleep
     */
    void getCycleInfo(uint64_t &totalCycle, uint64_t &activeTime, uint64_t &sleepTime);
    
    /**
     * @brief Verificar si es la primera ejecución (no despertar por timer)
     * @return true si es primera ejecución, false si despertó de sleep
     */
    bool isFirstBoot();
    
    /**
     * @brief Habilitar/deshabilitar salida por Serial
     * @param enable true para habilitar, false para deshabilitar
     */
    void enableSerial(bool enable);
    
    /**
     * @brief Configurar callback para logging personalizado
     * @param callback Función callback que recibe mensaje de log
     */
    void setLogCallback(LogCallback callback);
    
    /**
     * @brief Entrar en modo de emergencia (sleep corto)
     * @param emergencySeconds Segundos a dormir en emergencia (default: 30)
     */
    void emergencySleep(uint64_t emergencySeconds = 30);
    
    /**
     * @brief Obtener estadísticas de sleep
     * @return String con información de configuración actual
     */
    String getStatus();

private:
    /**
     * @brief Enviar mensaje de log
     * @param message Mensaje a enviar
     */
    void log(const char* message);
    
    /**
     * @brief Enviar mensaje de log con formato
     * @param format String de formato estilo printf
     * @param ... Argumentos variables
     */
    void logf(const char* format, ...);
};

#endif // DEEPSLEEP_MANAGER_H