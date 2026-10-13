/**
 * @file WatchDogManager.h
 * @brief Definición del gestor de watchdog y monitoreo de salud para ESP32
 * @details Este header contiene la clase WatchdogManager que proporciona supervisión
 *          completa del sistema mediante watchdog hardware/software, tracking persistente
 *          de errores en RTC Memory, sistema de puntuación de salud (0-100), y mecanismos
 *          de recuperación automática. Diseñado para sobrevivir deep sleep y mantener
 *          historial crítico de errores a través de resets.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#ifndef WATCHDOG_MANAGER_H
#define WATCHDOG_MANAGER_H

#include <Arduino.h>
#include "esp_task_wdt.h"
#include "esp_system.h"
#include <string.h>

/**
 * @class WatchdogManager
 * @brief Clase para manejo completo de Watchdog y Health Monitoring en ESP32
 * @details Proporciona un sistema robusto de supervisión que incluye:
 *          - Watchdog hardware con fallback a software
 *          - Tracking de errores persistente en RTC Memory (sobrevive deep sleep)
 *          - Sistema de puntuación de salud (0-100) basado en éxitos/fallos
 *          - Buffers categorizados por severidad (CRITICAL/WARNING/INFO)
 *          - Mecanismos de recuperación automática y manejo de emergencias
 *          - Callbacks configurables para logging y notificación de errores
 * 
 * Uso típico:
 * @code
 * WatchdogManager wdt(true);  // Habilitar Serial
 * wdt.begin();
 * 
 * void loop() {
 *     wdt.feedWatchdog();  // Alimentar cada <15s
 *     
 *     if (operacionExitosa()) {
 *         wdt.recordSuccess();
 *     } else {
 *         wdt.recordFailure();
 *     }
 *     
 *     if (wdt.hasCriticalFailures()) {
 *         wdt.handleEmergency();
 *     }
 * }
 * @endcode
 */

class WatchdogManager {
public:
    // ——— Códigos de Error ———

    /**
     * @enum error_code_t
     * @brief Códigos de error estandarizados del sistema
     */
    typedef enum {
        ERROR_NONE = 0, ///< Sin error (usado para marcar slots vacíos)
        ERROR_SENSOR_TIMEOUT = 1, ///< Timeout en operación de sensor (>5s sin respuesta)
        ERROR_SENSOR_INVALID_READING = 2, ///< Lectura de sensor fuera de rango o NaN
        ERROR_RTC_CORRUPTION = 3, ///< Datos corruptos en RTC Memory (CRC inválido)
        ERROR_RTC_WRITE_FAIL = 4, ///< Fallo al escribir en RTC Memory
        ERROR_MEMORY_FULL = 5, ///< Buffers de error llenos (pérdida de logs)
        ERROR_WDT_TIMEOUT = 6, ///< Watchdog hardware no alimentado a tiempo
        ERROR_SYSTEM_PANIC = 7, //< Sistema en estado de pánico (≥10 fallos consecutivos)
        ERROR_CRC_MISMATCH = 8, ///< Error de integridad en datos almacenados
        ERROR_WIFI_FAIL = 9, ///< Fallo en conexión WiFi
        ERROR_SENSOR_INIT_FAIL = 10, ///< Fallo en inicialización de sensor
        ERROR_MEMORY_LOW = 11, ///< Memoria heap < 10KB disponible
        ERROR_TIMING_ISSUE = 12 ///< >10 minutos sin operación exitosa
    } error_code_t;

    // ——— Severidad de Errores ———

    /**
     * @enum error_severity_t
     * @brief Niveles de severidad para clasificación de errores
     */
    typedef enum {
        SEVERITY_INFO = 0,
        SEVERITY_WARNING = 1,
        SEVERITY_CRITICAL = 2
    } error_severity_t;

    // ——— Estructura para logging de errores ———

    /**
     * @struct ErrorEntry
     * @brief Estructura empaquetada para almacenamiento compacto de errores en RTC Memory
     * @details Diseñada para minimizar uso de RTC Memory (6 bytes por entrada) mientras
     *          mantiene información crítica. Timestamp en minutos para ahorrar espacio.
     *          Contexto de 32 bits comprimido en 4 bytes para flexibilidad.
     */
    typedef struct __attribute__((packed)) {
        uint8_t error_code;         // Código de error
        uint8_t severity;           // Severidad del error
        uint16_t timestamp_min;     // Timestamp relativo en minutos
        uint8_t context[4];         // Contexto adicional del error
    } ErrorEntry;

    // ——— Callbacks ———

    /**
     * @typedef ErrorCallback
     * @brief Callback para notificación inmediata de errores
     * @param code Código de error
     * @param severity Severidad del error
     * @param context Información contextual de 32 bits
     * @note Se llama desde logError() antes de retornar.
     */
    typedef void (*ErrorCallback)(error_code_t code, error_severity_t severity, uint32_t context);
    
    /**
     * @typedef LogCallback
     * @brief Callback para redirección personalizada de logs
     * @param message Mensaje de texto a loguear
     * @note Si está configurado, mensajes NO se imprimen por Serial automáticamente.
     */
    typedef void (*LogCallback)(const char* message);

    // ——— Constantes ———

    /**
     * @brief Número máximo de errores CRITICAL almacenables en RTC Memory
     * @details Buffer circular: cuando está lleno, sobrescribe el error más antiguo.
     *          8 errores × 6 bytes = 48 bytes de RTC Memory.
     */
    static const int MAX_CRITICAL_ERRORS = 8;

    /**
     * @brief Número máximo de errores WARNING almacenables en RTC Memory
     * @details Buffer FIFO: cuando está lleno, hace shift descartando el más antiguo.
     *          16 errores × 6 bytes = 96 bytes de RTC Memory.
     */
    static const int MAX_WARNING_ERRORS = 16;

    /**
     * @brief Número máximo de errores INFO almacenables en RTC Memory
     * @details Buffer simple: cuando está lleno, descarta nuevos errores info.
     *          32 errores × 6 bytes = 192 bytes de RTC Memory.
     */
    static const int MAX_INFO_ERRORS = 32;

    /**
     * @brief Umbral de fallos consecutivos para considerar sistema en pánico
     * @details ≥10 fallos sin éxito intermedio → hasCriticalFailures() retorna true.
     */
    static const uint32_t MAX_CONSECUTIVE_FAILURES = 10;

private:
    // ——— Variables privadas ———

    /**
     * @brief Bandera para habilitar salida por Serial
     * @details true: Mensajes se imprimen por Serial (si _logCallback no está configurado)
     *          false: Modo silencioso
     */
    bool _enableSerialOutput;

    /**
     * @brief Puntero a función callback para logging personalizado
     * @details nullptr si no está configurado. Si está configurado, tiene prioridad sobre Serial.
     */
    LogCallback _logCallback;

    /**
     * @brief Puntero a función callback para notificación de errores
     * @details nullptr si no está configurado. Se llama después de cada logError().
     */
    ErrorCallback _errorCallback;

    /**
     * @brief Timestamp de la última verificación de salud (millis())
     * @details Actualizado en cada performHealthCheck(). Útil para detectar cuelgues en health checks.
     */
    uint32_t _lastHealthCheck;

    /**
     * @brief Bandera de estado de inicialización del watchdog
     * @details true: Watchdog (hardware o software) inicializado correctamente
     *          false: Watchdog no disponible (no llamar feedWatchdog())
     */
    bool _watchdogInitialized;

public:
    /**
     * @brief Constructor de WatchdogManager
     * @param enableSerial Habilitar salida por Serial (default: true)
     * @note Constructor no inicializa watchdog hardware. Llamar begin() en setup().
     */
    WatchdogManager(bool enableSerial = true);
    
    /**
     * @brief Inicializa el sistema de watchdog y monitoreo de salud
     * @details Configura Serial (si habilitado), inicializa watchdog hardware con fallback
     *          a software, inicializa variables RTC si es primera ejecución, e imprime
     *          estado inicial del sistema.
     * @note Debe llamarse una vez en setup() antes de cualquier otra operación del WatchdogManager.
     */
    void begin();
    
    /**
     * @brief Alimenta el watchdog hardware para evitar reset automático
     * @details Si watchdog hardware está disponible, llama esp_task_wdt_reset().
     *          Debe llamarse periódicamente (<15 segundos) en loop principal.
     * @note En modo software, esta función no hace nada (no hay reset automático).
     * @note Llamada segura incluso si watchdog no está inicializado (hace nada).
     */
    void feedWatchdog();
    
    /**
     * @brief Registra un error en el sistema con severidad y contexto
     * @details Crea ErrorEntry, almacena en buffer apropiado según severidad, incrementa
     *          contador total, y notifica mediante callback si está configurado.
     *          Errores se almacenan en RTC Memory y sobreviven deep sleep.
     * @param code Código de error (ver error_code_t)
     * @param severity Nivel de severidad (INFO/WARNING/CRITICAL)
     * @param context Información contextual de 32 bits (ej: voltaje*1000, tiempo ms, etc.)
     * @note Timestamp se guarda en minutos (millis()/60000) para ahorrar espacio RTC.
     */
    void logError(error_code_t code, error_severity_t severity, uint32_t context = 0);
    
    /**
     * @brief Realiza verificación completa de salud del sistema
     * @details Verifica memoria (checkMemoryHealth), timing (checkTimingHealth), y fallos
     *          consecutivos. Actualiza health score según resultado: incrementa si OK,
     *          decrementa si fallo.
     * @return true si sistema saludable (health >20% y fallos <5), false si crítico
     * @note Actualiza _lastHealthCheck con millis() actual.
     * @note Llamar periódicamente (ej: cada ciclo de medición) para monitoreo continuo.
     */
    bool performHealthCheck();
    
    /**
     * @brief Registra una operación exitosa en el sistema
     * @details Resetea contador de fallos consecutivos a 0, actualiza timestamp de última
     *          operación exitosa, e incrementa health score en 1 punto (hasta máx 100).
     * @note Llamar después de cada operación crítica exitosa (lectura sensores, envío WiFi, etc.).
     */
    void recordSuccess();
    
    /**
     * @brief Registra un fallo en una operación del sistema
     * @details Incrementa contador de fallos consecutivos y decrementa health score en 5 puntos.
     * @note Llamar después de cada operación crítica fallida.
     * @note Si fallos consecutivos ≥10, considerar llamar handleEmergency().
     */
    void recordFailure();
    
    /**
     * @brief Verifica si el sistema tiene fallos críticos que requieren acción inmediata
     * @return true si ≥10 fallos consecutivos O health score <10%, false en caso contrario
     * @note Usar para decidir si llamar handleEmergency() o attemptRecovery().
     */
    bool hasCriticalFailures();
    
    /**
     * @brief Obtiene el score actual de salud del sistema
     * @return Valor 0-100 representando salud del sistema (100=perfecto, 0=crítico)
     */
    uint32_t getHealthScore();
    
    /**
     * @brief Obtiene el número de fallos consecutivos actuales
     * @return Contador de fallos sin éxito intermedio
     */
    uint32_t getConsecutiveFailures();
    
    /**
     * @brief Intenta recuperación parcial del sistema
     * @details Limpia buffers WARNING/INFO (mantiene CRITICAL), reduce fallos consecutivos
     *          a la mitad, fija health score a 50%, y actualiza timestamp de última operación.
     * @return true siempre (indica que recovery fue intentado)
     * @note Usar cuando health score <30% pero sistema aún responde.
     * @note NO limpia errores críticos para mantener evidencia de problemas serios.
     */
    bool attemptRecovery();
    
    /**
     * @brief Maneja situación de emergencia del sistema (pánico)
     * @details Loguea ERROR_SYSTEM_PANIC como crítico, intenta attemptRecovery(),
     *          y si falla notifica mediante callback entrando en modo emergencia.
     * @note Llamar cuando hasCriticalFailures() retorna true.
     * @note En modo emergencia, sistema puede requerir reset manual o watchdog timeout.
     */
    void handleEmergency();
    
    /**
     * @brief Muestra log de errores almacenados en RTC Memory
     * @details Imprime todos los errores CRITICAL y últimos maxErrors WARNING (más recientes).
     *          Reconstruye contexto de 32 bits desde 4 bytes almacenados.
     * @param maxErrors Número máximo de warnings a mostrar (por defecto 3)
     * @note Errores INFO no se muestran (demasiado verbosos para display completo).
     */
    void displayErrorLog(int maxErrors = 3);
    
    /**
     * @brief Muestra estado completo de salud del sistema por Serial
     * @details Imprime: health score, fallos consecutivos, última operación exitosa,
     *          total errores, memoria libre, y estado del watchdog.
     * @note Útil para debugging y monitoreo en desarrollo.
     */
    void displaySystemHealth();
    
    /**
     * @brief Configura callback personalizado para logging de mensajes
     * @param callback Función con firma: void(const char* message)
     * @note Si callback está configurado, mensajes NO se imprimen por Serial automáticamente.
     * @note Útil para redirigir logs a display LCD, archivo SD, servidor remoto, etc.
     */
    void setLogCallback(LogCallback callback);
    
    /**
     * @brief Configura callback para notificación de errores en tiempo real
     * @param callback Función con firma: void(error_code_t, error_severity_t, uint32_t)
     * @note Se llama inmediatamente después de cada logError().
     * @note Útil para acciones inmediatas (ej: activar LED, enviar alerta, etc.).
     */
    void setErrorCallback(ErrorCallback callback);
    
    /**
     * @brief Habilita o deshabilita salida por Serial
     * @param enable true para habilitar, false para modo silencioso
     * @note Si callback está configurado, esta opción no tiene efecto.
     */
    void enableSerial(bool enable);
    
    /**
     * @brief Verifica si el watchdog está funcionando correctamente
     * @return true si watchdog inicializado y health score >30%, false en caso contrario
     * @note Health score <30% indica que watchdog puede no ser confiable.
     */
    bool isWatchdogHealthy();

private:
    /**
     * @brief Inicializa watchdog hardware del ESP32
     * @details Intenta: 1) Conectarse a watchdog existente, 2) Crear nuevo watchdog (15s),
     *          3) Fallback a modo software si todo falla.
     * @return true si inicialización exitosa (hardware O software), false solo si error grave
     * @note Watchdog hardware: Resetea ESP32 si no se alimenta en 15 segundos.
     * @note Modo software: Solo tracking, sin reset automático.
     */
    bool initializeHardwareWatchdog();
    
    /**
     * @brief Verifica salud de la memoria heap del ESP32
     * @details Si memoria libre < 10KB, loguea ERROR_MEMORY_LOW como warning.
     * @return true si memoria OK (≥10KB), false si memoria baja
     * @note 10KB es umbral conservador para operación estable con WiFi activo.
     */
    bool checkMemoryHealth();
    
    /**
     * @brief Verifica tiempo transcurrido desde última operación exitosa
     * @details Si han pasado >10 minutos sin éxito, loguea ERROR_TIMING_ISSUE.
     *          Detecta overflow de millis() (cada ~49 días) y resetea contador.
     * @return true si timing OK (<10 min), false si excede umbral
     * @note 10 minutos es umbral razonable para sistema con ciclos de medición frecuentes.
     */
    bool checkTimingHealth();
    
    /**
     * @brief Envía mensaje de log mediante callback o Serial
     * @param message Cadena de texto a imprimir
     * @note Si _logCallback configurado, lo usa; de lo contrario usa Serial si habilitado.
     */
    void log(const char* message);
    
    /**
     * @brief Envía mensaje de log con formato estilo printf
     * @param format Cadena de formato printf
     * @param ... Argumentos variables para format
     * @note Buffer interno de 256 caracteres. Mensajes más largos se truncan.
     * @note Usa vsnprintf para seguridad (previene buffer overflow).
     */
    void logf(const char* format, ...);
};

// ——— Variables RTC para persistencia ———

/**
 * @var wdt_system_health_score
 * @brief Puntuación de salud del sistema (0-100), externa para acceso desde main
 * @note Variable RTC_DATA_ATTR declarada en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR uint32_t wdt_system_health_score;

/**
 * @var wdt_consecutive_failures
 * @brief Contador de fallos consecutivos, externo para acceso desde main
 * @note Variable RTC_DATA_ATTR declarada en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR uint32_t wdt_consecutive_failures;

/**
 * @var wdt_last_successful_operation
 * @brief Timestamp de última operación exitosa, externo para acceso desde main
 * @note Variable RTC_DATA_ATTR declarada en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR uint32_t wdt_last_successful_operation;

/**
 * @var wdt_total_errors
 * @brief Contador total de errores, externo para acceso desde main
 * @note Variable RTC_DATA_ATTR declarada en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR uint16_t wdt_total_errors;

/**
 * @var wdt_critical_errors
 * @brief Buffer de errores críticos, externo para acceso desde main
 * @note Array RTC_DATA_ATTR declarado en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR WatchdogManager::ErrorEntry wdt_critical_errors[8];

/**
 * @var wdt_warning_errors
 * @brief Buffer de errores warning, externo para acceso desde main
 * @note Array RTC_DATA_ATTR declarado en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR WatchdogManager::ErrorEntry wdt_warning_errors[16];

/**
 * @var wdt_info_errors
 * @brief Buffer de errores info, externo para acceso desde main
 * @note Array RTC_DATA_ATTR declarado en .cpp, solo declaración extern aquí.
 */
extern RTC_DATA_ATTR WatchdogManager::ErrorEntry wdt_info_errors[32];

#endif // WATCHDOG_MANAGER_H