/**
 * @file WifiManager.h
 * @brief Definición del gestor de WiFi y WebSocket para ESP32
 * @details Este header contiene la clase WiFiManager que proporciona gestión completa
 *          de conexiones WiFi y WebSocket con servidor remoto, envío de datos de sensores
 *          en formato JSON, y dos modos de operación: automático (envío inmediato) y
 *          manual (espera solicitud del servidor). Integrado con RTCMemory para acceso
 *          a datos almacenados y WatchdogManager para monitoreo de salud.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "RTCMemory.h"
#include "WatchDogManager.h"
#include "RTC.h"
#include "CalibrationManager.h"

/**
 * @class WiFiManager
 * @brief Clase para manejo eficiente de WiFi y WebSocket en ESP32
 * @details Proporciona un sistema robusto de comunicación que incluye:
 *          - Gestión de conexiones WiFi con timeout configurable
 *          - Comunicación WebSocket bidireccional con servidor
 *          - Envío de datos de sensores en formato JSON
 *          - Modo automático: Envía datos inmediatamente al conectar
 *          - Modo manual: Espera solicitud del servidor antes de enviar
 *          - Integración con RTCMemory para acceso a datos almacenados
 *          - Integración con WatchdogManager para monitoreo de salud
 *          - Callbacks configurables para logging, errores y cambios de estado
 *          - Gestión de desconexión y modo bajo consumo
 * 
 * Uso típico (modo manual):
 * @code
 * wifi_config_t config = {
 *     .ssid = "MiRed",
 *     .password = "MiPassword",
 *     .server_ip = "192.168.1.100",
 *     .server_port = 8080,
 *     .connect_timeout_ms = 10000,
 *     .websocket_timeout_ms = 10000,
 *     .max_retry_attempts = 3
 * };
 * 
 * WiFiManager wifiMgr(true);
 * wifiMgr.begin(config);
 * wifiMgr.setManagers(&rtcMemory, &watchdog);
 * 
 * // Modo manual: Espera solicitud del servidor
 * wifiMgr.transmitDataManual(120, 60000);
 * @endcode
 */

struct CalibrationValues {
    float ph_offset = 0.0f;
    float ec_offset = 0.0f;
    float turbidity_offset = 0.0f;
    // Agrega más sensores según tu proyecto
};
class WiFiManager {
public:
    // ——— Estados de conexión ———

    /**
     * @enum wifi_status_t
     * @brief Códigos de estado para conexión WiFi y WebSocket
     */
    typedef enum {
        //estados posibles de la conexión wifi/websocket
        WIFI_DISCONNECTED = 0, ///< WiFi desconectado (estado inicial o después de disconnect())
        WIFI_CONNECTING = 1, ///< Intentando conectar a red WiFi
        WIFI_CONNECTED = 2, ///< WiFi conectado exitosamente
        WIFI_ERROR = 3, ///< Error en conexión WiFi (timeout o fallo)
        WEBSOCKET_CONNECTING = 4, ///< Intentando conectar WebSocket al servidor
        WEBSOCKET_CONNECTED = 5, ///< WebSocket conectado exitosamente
        WEBSOCKET_ERROR = 6, ///< Error en conexión WebSocket (timeout o fallo)
        DATA_SENDING = 7, ///< Enviando datos al servidor
        DATA_SENT = 8, ///< Datos enviados exitosamente
        DATA_ERROR = 9 ///< Error enviando datos (parcial o total)
    } wifi_status_t;

    // ——— Configuración WiFi ———

    /**
     * @struct wifi_config_t
     * @brief Estructura de configuración para WiFi y WebSocket
     */
    typedef struct {
        //Estructura de configuración basica de wifi y websocket
        const char* ssid;
        const char* password;
        const char* server_ip;
        uint16_t server_port;
        uint32_t connect_timeout_ms;
        uint32_t websocket_timeout_ms;
        uint32_t max_retry_attempts;
    } wifi_config_t;

    // ——— Callbacks ———

    /**
     * @typedef LogCallback
     * @brief Callback para redirección personalizada de logs
     * @param message Mensaje de texto a loguear
     */
    typedef void (*LogCallback)(const char* message);

    /**
     * @typedef ErrorCallback
     * @brief Callback para notificación de errores
     * @param code Código de error watchdog
     * @param severity Severidad del error
     * @param context Información contextual
     */
    typedef void (*ErrorCallback)(WatchdogManager::error_code_t code, WatchdogManager::error_severity_t severity, uint32_t context);
    
    /**
     * @typedef StatusCallback
     * @brief Callback para notificación de cambios de estado
     * @param status Nuevo estado WiFi/WebSocket
     * @param message Mensaje descriptivo opcional
     */
    typedef void (*StatusCallback)(wifi_status_t status, const char* message);

private:
    // ——— Variables de configuración ———

    /**
     * @brief Configuración WiFi y WebSocket actual
     */
    wifi_config_t _config;

    /**
     * @brief Bandera para habilitar salida por Serial
     */
    bool _enableSerialOutput;
    
    // ——— Variables de estado ———

    /**
     * @brief Estado actual de conexión WiFi/WebSocket
     */
    wifi_status_t _currentStatus;

    /**
     * @brief Bandera de inicialización exitosa con begin()
     */
    bool _wifiInitialized;

    /**
     * @brief Bandera de conexión WebSocket activa (actualizada por callback)
     */
    bool _websocketConnected;

    /**
     * @brief Timestamp de inicio de conexión WiFi (millis())
     */
    uint32_t _connectionStartTime;

    /**
     * @brief Contador acumulativo de lecturas enviadas exitosamente
     */
    uint32_t _totalDataSent;

    /**
     * @brief Último código de error registrado
     */
    uint32_t _lastErrorCode;
    
    // ——— Callbacks ———

    /**
     * @brief Puntero a función callback para logging personalizado
     */
    LogCallback _logCallback;

    /**
     * @brief Puntero a función callback para notificación de errores
     */
    ErrorCallback _errorCallback;

    /**
     * @brief Puntero a función callback para cambios de estado
     */
    StatusCallback _statusCallback;
    
    // ——— Referencias a otros managers ———

    /**
     * @brief Puntero a RTCMemoryManager para acceso a datos almacenados
     */
    RTCMemoryManager* _rtcMemory;

    /**
     * @brief Puntero a WatchdogManager para monitoreo de salud
     */
    WatchdogManager* _watchdog;
    CalibrationManager* _calibrationManager;
    
    // ——— WebSocket ———

    /**
     * @brief Objeto WebSocketsClient de la librería
     */
    WebSocketsClient _webSocket;

    /**
     * @brief Bandera de confirmación de recepción de datos por servidor
     */
    bool _dataTransmissionComplete;

    /**
     * @brief Última respuesta textual recibida del servidor
     */
    String _lastServerResponse;

public:
    /**
     * @brief Constructor del WiFiManager
     * @param enableSerial Habilitar salida por Serial (default: true)
     * @note Constructor no inicializa WiFi hardware. Llamar begin() en setup().
     */
    WiFiManager(bool enableSerial = true);
    
    /**
     * @brief Inicializa el WiFiManager con configuración WiFi y WebSocket
     * @param config Estructura wifi_config_t con SSID, password, servidor, etc.
     * @note Debe llamarse una vez en setup() antes de cualquier operación WiFi.
     * @note Configura WiFi en modo Station (WIFI_STA).
     */
    void begin(const wifi_config_t &config);
    
    /**
     * @brief Configura referencias a RTCMemoryManager y WatchdogManager
     * @param rtcMemory Puntero a RTCMemoryManager para acceso a datos almacenados
     * @param watchdog Puntero a WatchdogManager para monitoreo de salud
     * @note Método para inyección de dependencias. Los punteros deben ser válidos.
     */
    void setManagers(RTCMemoryManager* rtcMemory, WatchdogManager* watchdog);
    
    /**
     * @brief Conecta a red WiFi con timeout configurado
     * @return true si conexión exitosa, false si timeout o error
     * @note Función bloqueante hasta conectar o timeout (connect_timeout_ms).
     * @note Alimenta watchdog durante espera para evitar reset.
     */
    bool connectWiFi();
    
    /**
     * @brief Conecta WebSocket al servidor configurado con timeout
     * @return true si conexión exitosa, false si timeout o error
     * @note Función bloqueante hasta conectar o timeout (websocket_timeout_ms).
     * @note La conexión real se detecta mediante callback webSocketEvent().
     */
    bool connectWebSocket();
    
    /**
     * @brief Envía todos los datos almacenados en RTC Memory al servidor
     * @param maxReadings Máximo número de lecturas a enviar (default: 120)
     * @return true si envío exitoso (total o parcial con >0 lecturas), false si error crítico
     * @note Buffer local de 120 lecturas. Modificar en .cpp para mayor capacidad.
     * @note Marca datos como enviados en RTCMemory si envío exitoso.
     * @warning Función bloqueante. Puede tardar varios minutos con muchas lecturas.
     */
    bool sendStoredData(int maxReadings = 160);
    
    /**
     * @brief Envía una lectura de sensor específica al servidor vía WebSocket
     * @param reading Estructura SensorReading a enviar
     * @return true si envío exitoso, false si error
     * @note Comportamiento depende del modo (manual: sin espera, automático: espera confirmación).
     */
    bool sendReading(const RTCMemoryManager::SensorReading &reading);
    
    /**
     * @brief Desconecta WiFi, WebSocket y apaga radio WiFi (modo bajo consumo)
     * @note Importante para ahorro de energía antes de deep sleep.
     * @note WiFi.mode(WIFI_OFF) reduce consumo significativamente.
     */
    void disconnect();
    
    /**
     * @brief Obtiene estado actual de conexión
     * @return Enum wifi_status_t con estado actual
     */
    wifi_status_t getStatus();
    
    /**
     * @brief Obtiene descripción textual del estado actual
     * @return String con descripción legible del estado
     */
    String getStatusString();
    
    /**
     * @brief Verifica si WiFi está conectado actualmente
     * @return true si WiFi.isConnected() retorna true
     */
    bool isWiFiConnected();
    
    /**
     * @brief Configurar referencia al CalibrationManager
     */
    void setCalibrationManager(CalibrationManager* calibManager);


    /**
     * @brief Verificar si WebSocket está conectado
     * @return true si está conectado
     */
    bool isWebSocketConnected();
    
    /**
     * @brief Obtiene información detallada de conexión WiFi
     * @return String con IP, RSSI y SSID si conectado, "WiFi desconectado" si no
     */
    String getConnectionInfo();
    
    /**
     * @brief Configura callback personalizado para logging
     * @param callback Función con firma: void(const char* message)
     */
    void setLogCallback(LogCallback callback);

    /**
     * @brief Configura callback para notificación de errores
     * @param callback Función con firma: void(error_code_t, error_severity_t, uint32_t)
     */
    void setErrorCallback(ErrorCallback callback);

    /**
     * @brief Configura callback para cambios de estado
     * @param callback Función con firma: void(wifi_status_t, const char*)
     */
    void setStatusCallback(StatusCallback callback);
    
    /**
     * @brief Habilita o deshabilita salida por Serial
     * @param enable true para habilitar, false para modo silencioso
     */
    void enableSerial(bool enable);
    
    /**
     * @brief Proceso completo automático: conectar, enviar datos inmediatamente y desconectar
     * @param maxReadings Máximo número de lecturas a enviar (default: 10)
     * @return true si todo el proceso fue exitoso, false si error
     * @note NO espera solicitud del servidor, envía inmediatamente al conectar.
     * @note Registra éxito/fallo en watchdog.
     */
    bool transmitData(int maxReadings = 10);
    
    // ===== NUEVAS FUNCIONES PARA MODO MANUAL =====
    
    /**
     * @brief Espera solicitud de descarga de datos del servidor (modo manual)
     * @param timeout_ms Tiempo máximo de espera en milisegundos (default: 60000 = 1 min)
     * @return true si se recibió solicitud "request_all_data", false si timeout
     * @note Función bloqueante hasta recibir solicitud o timeout.
     * @note Esencial para modo manual (esperar comando del servidor).
     */
    bool waitForDataRequest(uint32_t timeout_ms = 60000);
    
    /**
     * @brief Proceso completo manual: conectar, esperar solicitud, enviar si la hay
     * @param maxReadings Máximo número de lecturas a enviar (default: 120)
     * @param waitTimeout Timeout esperando solicitud del servidor en ms (default: 60000)
     * @return true si proceso exitoso (incluso si no había solicitud), false si error crítico
     * @note Diseñado para ciclos de deep sleep donde servidor controla cuándo descargar.
     * @note Éxito si conecta aunque no haya solicitud (permite verificar conectividad).
     * @note Registra éxito/fallo en watchdog.
     */
    bool transmitDataManual(int maxReadings = 160, uint32_t waitTimeout = 60000);
    
    /**
     * @brief Configura modo de operación (manual o automático)
     * @param manual true para modo manual (espera solicitud), false para automático
     * @note Afecta comportamiento de transmitDataManual() vs transmitData().
     */
    void setManualMode(bool manual);
    
    /**
     * @brief Obtiene modo actual de operación
     * @return true si está en modo manual, false si automático
     */
    bool isManualMode();
    
    /**
     * @brief Obtiene estadísticas completas de transmisión
     * @return String formateado con estado, modo, datos enviados, errores y conexión
     */
    String getTransmissionStats();

private:
    /**
     * @brief Callback para eventos del WebSocket
     * @param type Tipo de evento (conectar, desconectar, mensaje, error)
     * @param payload Datos del evento
     * @param length Longitud de payload
     * @note Maneja WStype_DISCONNECTED, WStype_CONNECTED, WStype_TEXT, WStype_ERROR.
     * @note En modo manual, filtra mensajes para mostrar solo importantes.
     */
    void webSocketEvent(WStype_t type, uint8_t* payload, size_t length);
    
    /**
     * @brief Crea mensaje JSON con datos de lectura y metadata del sistema
     * @param reading Estructura SensorReading a serializar
     * @return String con JSON formateado (device_id, timestamp, sensores, sistema)
     * @note Buffer StaticJsonDocument<400>. Aumentar si JSON más grande.
     */
    String createDataJSON(const RTCMemoryManager::SensorReading &reading);
    
    /**
     * @brief Actualiza estado interno y notifica mediante callback si configurado
     * @param status Nuevo estado wifi_status_t
     * @param message Mensaje descriptivo opcional
     */
    void updateStatus(wifi_status_t status, const char* message = nullptr);
    
    /**
     * @brief Reporta error al watchdog y mediante callback si configurados
     * @param code Código de error watchdog
     * @param severity Severidad del error
     * @param context Información contextual
     */
    void reportError(WatchdogManager::error_code_t code, WatchdogManager::error_severity_t severity, uint32_t context);
    
    /**
     * @brief Envía mensaje de log mediante callback o Serial
     * @param message Cadena de texto a imprimir
     */
    void log(const char* message);

    /**
     * @brief Envía mensaje de log con formato estilo printf
     * @param format Cadena de formato printf
     * @param ... Argumentos variables para format
     */
    void logf(const char* format, ...);
    
    /**
     * @brief Función estática para eventos WebSocket (requerida por librería)
     * @note NO USADA actualmente. Se usa lambda en begin() en su lugar.
     * @deprecated Mantener por compatibilidad, pero lambda preferida.
     */
    static void staticWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
    
    // ——— Variable estática para acceso desde callback estático ———

    /**
     * @brief Puntero estático a instancia del WiFiManager para callback WebSocket
     * @note Necesario para callback estático. Patrón Singleton.
     */
    static WiFiManager* _instance;
};

#endif // WIFI_MANAGER_H