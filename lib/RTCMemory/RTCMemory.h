#ifndef RTCMEMORY_MANAGER_H
#define RTCMEMORY_MANAGER_H

#include <Arduino.h>
#include "esp_crc.h"
#include <string.h>

/**
 * @file RTCMemory.h
 * @brief Definición de la clase RTCMemoryManager para manejo de RTC Memory en ESP32.
 * 
 * Esta clase se enfoca únicamente en el almacenamiento persistente de datos en 
 * la RTC Memory del ESP32, con soporte para validación CRC, buffer circular de 
 * lecturas y funciones auxiliares de depuración.
 * 
 * @author Daniel Acosta - Santiago Erazo
 * @version 1.0
 * @date 2025-10-01
 */

/**
 * @brief Clase para manejo PURO de RTC Memory en ESP32
 * 
 * Implementa un sistema de almacenamiento circular y persistente en la RTC Memory
 * del ESP32. Soporta validación de integridad mediante CRC, estructuras empaquetadas
 * y herramientas de logging/debug.
 */
class RTCMemoryManager {
public:

    /**
     * @brief Estructura que representa una lectura de sensores.
     */
    typedef struct __attribute__((packed)) {
        uint32_t timestamp;         // Tiempo en milisegundos (desde boot)
        uint32_t rtc_timestamp;     // Timestamp Unix del RTC
        float temperature;          // Temperatura en °C
        float ph;                   // pH del agua
        float turbidity;            // Turbidez en NTU
        float tds;    
        float ec;              // TDS en ppm
        uint16_t reading_number;    // Número de lectura
        uint8_t sensor_status;      // Estado de los sensores (flags)
        bool valid;                 // Indica si la lectura es válida
    } SensorReading;

    /**
     * @brief Estructura principal de datos RTC persistentes.
     */
    typedef struct __attribute__((packed)) {
        uint32_t magic_start;       // 0x12345678
        uint32_t sequence_number;   // Número de secuencia
        uint32_t boot_timestamp;    // Timestamp del último boot
        uint32_t header_crc;        // CRC del header
        SensorReading readings[160]; // Lecturas de sensores (buffer circular)
        uint32_t data_crc;          // CRC de todos los datos
        uint32_t magic_end;         // 0x87654321
    } RTCDataStructure;

private:
    static const uint32_t MAGIC_START = 0x12345678;
    static const uint32_t MAGIC_END = 0x87654321;
    static const int MAX_READINGS = 160;
    static const uint32_t MAX_TOTAL_READINGS = 10000;  // Valor máximo lógico
    
    bool _enableSerialOutput;
    
    /**
     * @brief Callback opcional para logging externo.
     */
    typedef void (*LogCallback)(const char* message);
    LogCallback _logCallback;

public:
    /**
     * @brief Constructor de la clase RTCMemoryManager
     * @param enableSerial Habilitar mensajes por Serial (default: true)
     */
    RTCMemoryManager(bool enableSerial = true);
    
    /**
     * @brief Inicializar el gestor de RTC Memory
     */
    void begin();
    
    /**
     * @brief Validar integridad de datos en RTC Memory
     * @return true si los datos son válidos, false si están corruptos
     */
    bool validateIntegrity();
    
    /**
     * @brief Inicializar estructura de datos RTC (primera ejecución o corrupción)
     */
    void initialize();
    
    /**
     * @brief Almacenar lectura de sensores con validación
     * @param reading Estructura con datos de sensores
     * @return true si se almacenó correctamente, false en caso de error
     */
    bool storeReading(const SensorReading &reading);
    
    
    /**
     * @brief Crear una lectura completa de todos los sensores
     * @param temperature Temperatura en °C
     * @param ph Valor de pH
     * @param turbidity Turbidez en NTU
     * @param tds TDS en ppm
     * @param ec Conductividad eléctrica en µS/cm
     * @param sensorStatus Estado de sensores (default: 0)
     * @return Estructura SensorReading completa
     */
    SensorReading createFullReading(float temperature, float ph, float turbidity, float tds, float ec, uint8_t sensorStatus = 0);
    
    /**
     * @brief Obtener número total de lecturas realizadas
     * @return Contador total de lecturas
     */
    uint16_t getTotalReadings();
    
    /**
     * @brief Obtener índice actual del buffer circular
     * @return Posición actual en el buffer
     */
    int getCurrentIndex();
    
    /**
     * @brief Obtener número de secuencia actual
     * @return Número de secuencia para tracking de operaciones
     */
    uint32_t getSequenceNumber();
    
    /**
     * @brief Verificar si el sistema necesita enviar datos por WiFi
     * @param readingsThreshold Umbral de lecturas para enviar (default: 10)
     * @return true si debe enviar datos
     */
    bool shouldSendData(int readingsThreshold = 10);
    
    /**
     * @brief Marcar que los datos fueron enviados (resetea contador)
     */
    void markDataSent();
    
    /**
     * @brief Obtener última lectura almacenada
     * @param reading Referencia donde almacenar la lectura
     * @return true si hay lectura válida, false si no hay datos
     */
    bool getLastReading(SensorReading &reading);
    
    /**
     * @brief Obtener múltiples lecturas más recientes
     * @param readings Array donde almacenar las lecturas
     * @param maxReadings Máximo número de lecturas a obtener
     * @return Número de lecturas realmente obtenidas
     */
    int getRecentReadings(SensorReading readings[], int maxReadings);
    
    /**
     * @brief Mostrar lecturas almacenadas por Serial
     * @param numReadings Número de lecturas a mostrar (default: 3)
     */
    void displayStoredReadings(int numReadings = 3);
    
    /**
     * @brief Forzar reset completo de todos los datos RTC
     *  CUIDADO: Elimina todos los datos almacenados
     */
    void forceCompleteReset();
    
    /**
     * @brief Obtener estadísticas del sistema RTC
     * @return String con información del almacenamiento RTC
     */
    String getStatus();
    
    /**
     * @brief Obtener información de uso de memoria
     * @return String con estadísticas de uso de buffers
     */
    String getMemoryUsage();
    
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
     * @brief Verificar si RTC Memory está inicializada correctamente
     * @return true si está inicializada
     */
    bool isInitialized();

private:
    /**
     * @brief Calcular CRC32 de un bloque de datos
     * @param data Puntero a los datos
     * @param length Longitud de los datos
     * @return Valor CRC32 calculado
     */
    uint32_t calculateCRC32(const void* data, size_t length);
    
    /**
     * @brief Actualizar CRCs después de modificar datos
     */
    void updateCRCs();
    
    /**
     * @brief Verificar rangos lógicos de variables RTC
     * @return true si los rangos son válidos
     */
    bool validateLogicalRanges();
    
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

#endif // RTCMEMORY_MANAGER_H