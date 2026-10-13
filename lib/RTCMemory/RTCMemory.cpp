/**
 * @file RTCMemory.cpp
 * @brief Implementación de la clase RTCMemoryManager para gestión de memoria RTC en ESP32.
 *
 * Este módulo administra la memoria RTC del ESP32 para almacenar y recuperar lecturas de sensores 
 * de manera persistente entre reinicios o ciclos de energía. 
 * Incluye validación de integridad con CRC, gestión de estructuras de datos, 
 * almacenamiento circular de lecturas, recuperación cronológica y funciones de depuración.
 *
 * Dependencias:
 *   - Arduino Core para ESP32
 *   - Funciones de CRC hardware (esp_crc32_le)
 *   - Librerías estándar de C para manejo de memoria y strings
 *
 * Funcionalidades principales:
 *   - Almacenamiento y validación de lecturas de sensores
 *   - Validación de integridad mediante magic numbers y CRC32
 *   - Soporte de buffer circular con control de índices
 *   - Recuperación de lecturas recientes y estado del sistema
 *   - Reset completo de estructura RTC
 *   - Logging configurable por Serial o callback externo
 *
 * @author Daniel Acosta - Santiago Erazo
 * @version 1.0
 * @date 2025-10-01
 */

#include "RTCMemory.h"
#include <stdarg.h>

// ——— Variables en RTC Memory ———
RTC_DATA_ATTR RTCMemoryManager::RTCDataStructure rtc_data; /**< Estructura principal almacenada en memoria RTC */
RTC_DATA_ATTR int currentIndex = 0; /**< Estructura principal almacenada en memoria RTC */
RTC_DATA_ATTR uint16_t totalReadings = 0; /**< Total de lecturas almacenadas */

/**
 * @brief Constructor de la clase.
 * @param enableSerial Indica si se habilita la salida por Serial.
 */
RTCMemoryManager::RTCMemoryManager(bool enableSerial) 
    : _enableSerialOutput(enableSerial), _logCallback(nullptr) {
}

/**
 * @brief Inicializa el sistema de memoria RTC y Serial si corresponde.
 */
void RTCMemoryManager::begin() {
    if (_enableSerialOutput && !Serial) {
        Serial.begin(115200);
        delay(100);
    }
    //log(" RTC Memory Manager Inicializado ");
}

/**
 * @brief Valida la integridad de la memoria RTC.
 * @return true si los datos son válidos, false en caso contrario.
 */
bool RTCMemoryManager::validateIntegrity() {
    //log(" Validando integridad RTC...");
    
    // Verificar magic numbers
    if (rtc_data.magic_start != MAGIC_START || rtc_data.magic_end != MAGIC_END) {
        //log(" Magic numbers inválidos");
        return false;
    }
    
    // Verificar CRC del header
    uint32_t calculated_header_crc = calculateCRC32(&rtc_data.sequence_number, 
                                                   sizeof(uint32_t) * 2);
    if (rtc_data.header_crc != calculated_header_crc) {
        //logf(" Header CRC mismatch: calc=0x%08X, stored=0x%08X", 
          //   calculated_header_crc, rtc_data.header_crc);
        return false;
    }
    
    // Verificar CRC de datos
    uint32_t calculated_data_crc = calculateCRC32(&rtc_data.readings, 
                                                sizeof(rtc_data.readings));
    if (rtc_data.data_crc != calculated_data_crc) {
        //logf(" Data CRC mismatch: calc=0x%08X, stored=0x%08X", 
          //   calculated_data_crc, rtc_data.data_crc);
        return false;
    }
    
    // Verificar rangos lógicos
    if (!validateLogicalRanges()) {
        return false;
    }
    
    //log(" Validación RTC exitosa");
    return true;
}

/**
 * @brief Inicializa la estructura de memoria RTC con valores por defecto.
 */

// Inicializar datos RTC
void RTCMemoryManager::initialize() {
    //log(" Inicializando estructura RTC...");
    
    // Limpiar toda la estructura
    memset(&rtc_data, 0, sizeof(RTCDataStructure));
    
    // Configurar magic numbers
    rtc_data.magic_start = MAGIC_START;
    rtc_data.magic_end = MAGIC_END;
    
    // Inicializar metadatos
    rtc_data.sequence_number = 1;
    rtc_data.boot_timestamp = millis();
    
    // Calcular CRCs iniciales
    updateCRCs();
    
    // Resetear contadores
    currentIndex = 0;
    totalReadings = 0;
    
    log(" RTC Memory inicializada correctamente");
}

/**
 * @brief Almacena una lectura de sensor en la memoria RTC.
 * @param reading Lectura de sensor a guardar.
 * @return true si se almacena correctamente, false en caso contrario.
 */
// Almacenar lectura de sensores con validación
bool RTCMemoryManager::storeReading(const SensorReading &reading) {
    if (!reading.valid) {
        log(" No se almacena lectura inválida");
        return false;
    }
    
    // Intentar escribir con verificación
    uint32_t start_time = millis();
    
    // Copia de seguridad antes de escribir
    SensorReading backup;
    memcpy(&backup, (void*)&rtc_data.readings[currentIndex], sizeof(SensorReading));
    
    // Escribir datos
    memcpy((void*)&rtc_data.readings[currentIndex], &reading, sizeof(SensorReading));
    
    // Verificar escritura inmediatamente
    SensorReading verification;
    memcpy(&verification, (void*)&rtc_data.readings[currentIndex], sizeof(SensorReading));
    
    // Verificar timeout de escritura
    if (millis() - start_time > 100) {  // 100ms timeout para escritura
        logf(" Escritura RTC lenta: %u ms", millis() - start_time);
        // Nota: Error reporting manejado por WatchdogManager
    }
    
    // Verificar integridad de la escritura
    if (memcmp(&reading, &verification, sizeof(SensorReading)) != 0) {
        log(" Fallo en verificación de escritura RTC");
        // Restaurar backup
        memcpy((void*)&rtc_data.readings[currentIndex], &backup, sizeof(SensorReading));
        // Nota: Error reporting manejado por WatchdogManager
        return false;
    }
    
    // Actualizar metadatos
    rtc_data.sequence_number++;
    totalReadings++;
    
    // Actualizar CRCs
    updateCRCs();
    
    logf(" Lectura #%d almacenada en posición %d", 
        reading.reading_number, currentIndex);
    
    // Actualizar índice (buffer circular)
    currentIndex = (currentIndex + 1) % MAX_READINGS;
    
    return true;
}

/**
 * @brief Crea una lectura completa de sensores validada.
 * @param temperature Temperatura en °C.
 * @param ph Valor de pH.
 * @param turbidity Turbidez.
 * @param tds Sólidos disueltos totales (ppm).
 * @param ec Conductividad eléctrica (µS/cm).
 * @param sensorStatus Estado del sensor.
 * @return Objeto SensorReading con validación de rangos.
 */

// Crear lectura completa
RTCMemoryManager::SensorReading RTCMemoryManager::createFullReading(float temperature, float ph, float turbidity, float tds, float ec, uint8_t sensorStatus) {
    SensorReading reading = {0};
    reading.timestamp = millis();
    reading.temperature = temperature;
    reading.ph = ph;
    reading.turbidity = turbidity;
    reading.tds = tds;
    reading.ec = ec;
    reading.reading_number = totalReadings + 1;
    reading.sensor_status = sensorStatus;
    
    // Validar rangos de cada sensor
    bool tempValid = (temperature > -50.0 && temperature < 85.0);
    bool phValid = (ph >= 0.0 && ph <= 14.0);
    bool turbidityValid = (turbidity >= 0.0 && turbidity <= 3000.0);
    bool tdsValid = (tds >= 0.0 && tds <= 2000.0);
    bool ecValid = (ec >= 0.0 && ec <= 4000.0);
    
    reading.valid = tempValid && phValid && turbidityValid && tdsValid && ecValid;    ;
    
    return reading;
}

// Getters básicos

/** @brief Obtiene el número total de lecturas almacenadas. */
uint16_t RTCMemoryManager::getTotalReadings() { return totalReadings; }

/** @brief Obtiene el índice actual del buffer circular. */
int RTCMemoryManager::getCurrentIndex() { return currentIndex; }

/** @brief Obtiene el número de secuencia actual. */
uint32_t RTCMemoryManager::getSequenceNumber() { return rtc_data.sequence_number; }



/**
 * @brief Verifica si se deben enviar datos según un umbral de lecturas.
 * @param readingsThreshold Umbral de lecturas.
 * @return true si se deben enviar, false de lo contrario.
 */

bool RTCMemoryManager::shouldSendData(int readingsThreshold) {
    return (totalReadings % readingsThreshold == 0) && (totalReadings > 0);
}

/**
 * @brief Marca que los datos ya fueron enviados.
 */
void RTCMemoryManager::markDataSent() {
    rtc_data.sequence_number++;
    updateCRCs();
    log(" Datos marcados como enviados");
}

/**
 * @brief Obtiene la última lectura almacenada.
 * @param reading Referencia donde se guarda la lectura.
 * @return true si existe lectura válida, false en caso contrario.
 */
bool RTCMemoryManager::getLastReading(SensorReading &reading) {
    if (totalReadings == 0) {
        return false;
    }
    
    int lastIndex = (currentIndex - 1 + MAX_READINGS) % MAX_READINGS;
    memcpy(&reading, (void*)&rtc_data.readings[lastIndex], sizeof(SensorReading));
    
    return reading.valid;
}

// Obtener múltiples lecturas recientes
/**
 * @brief Recupera un conjunto de lecturas recientes desde la memoria RTC.
 * 
 * @param readings Arreglo de destino donde se almacenarán las lecturas.
 * @param maxReadings Número máximo de lecturas a recuperar.
 * @return int Cantidad de lecturas recuperadas correctamente.
 */
int RTCMemoryManager::getRecentReadings(SensorReading readings[], int maxReadings) {
    int count = 0;
    int totalAvailable = (totalReadings < MAX_READINGS) ? totalReadings : MAX_READINGS;
    int toRetrieve = (maxReadings < totalAvailable) ? maxReadings : totalAvailable;
    
    logf(" getRecentReadings: Solicitados=%d, Disponibles=%d, ARecuperar=%d", 
        maxReadings, totalAvailable, toRetrieve);
    
    // Crear array dinámico del tamaño correcto
    SensorReading* tempBuffer = new SensorReading[toRetrieve];
    int tempCount = 0;
    
    // Recopilar lecturas válidas desde la más reciente hacia atrás
    for (int i = 0; i < toRetrieve && tempCount < toRetrieve; i++) {
        int index = (currentIndex - 1 - i + MAX_READINGS) % MAX_READINGS;
        SensorReading temp;
        memcpy(&temp, (void*)&rtc_data.readings[index], sizeof(SensorReading));
        
        if (temp.valid && temp.reading_number > 0) {
            tempBuffer[tempCount] = temp;
            tempCount++;
        }
    }
    
    logf(" Lecturas válidas recuperadas: %d de %d solicitadas", tempCount, toRetrieve);
    
    // Copiar en orden cronológico (reversa del buffer)
    for (int i = tempCount - 1; i >= 0; i--) {
        readings[count] = tempBuffer[i];
        count++;
    }
    
    // Liberar memoria
    delete[] tempBuffer;
    
    logf(" getRecentReadings completado: %d lecturas retornadas", count);
    return count;
}

// Mostrar lecturas almacenadas
/**
 * @brief Muestra en el log las últimas lecturas almacenadas en la memoria RTC.
 * 
 * @param numReadings Cantidad de lecturas a mostrar.
 */
void RTCMemoryManager::displayStoredReadings(int numReadings) {
    log("\n --- DATOS ALMACENADOS EN RTC MEMORY ---");
    logf("Total lecturas: %d | Posición actual: %d", 
        totalReadings, currentIndex);
    
    // Mostrar últimas N lecturas válidas
    int shown = 0;
    log("Últimas lecturas:");
    
    for (int i = 0; i < MAX_READINGS && shown < numReadings; i++) {
        int index = (currentIndex - 1 - i + MAX_READINGS) % MAX_READINGS;
        SensorReading temp;
        memcpy(&temp, (void*)&rtc_data.readings[index], sizeof(SensorReading));
        
        if (temp.valid && temp.reading_number > 0) {
            logf("  [%d] #%d: T:%.1f°C pH:%.1f Turb:%.1f TDS:%.0f EC:%.1f | Status:0x%02X | %ums",
                index, temp.reading_number, temp.temperature, temp.ph, 
                temp.turbidity, temp.tds, temp.ec, temp.sensor_status, temp.timestamp);
            shown++;
        }
    }
    
    if (shown == 0) {
        log("   No hay lecturas válidas");
    }
    
    log("---------------------------------------");
}

// Reset completo
/**
 * @brief Fuerza un reinicio completo de la memoria RTC eliminando todo su contenido.
 */
void RTCMemoryManager::forceCompleteReset() {
    log(" FORZANDO RESET COMPLETO DEL SISTEMA...");
    log(" Todos los datos RTC serán eliminados");
    
    // Limpiar completamente toda la estructura RTC
    memset(&rtc_data, 0, sizeof(RTCDataStructure));
    currentIndex = 0;
    totalReadings = 0;
    
    log(" Reset completo realizado");
    log("El sistema se reiniciará como primera ejecución");
}

// Obtener estado
/**
 * @brief Devuelve un resumen del estado del administrador de memoria RTC.
 * 
 * @return String Estado formateado en texto.
 */
String RTCMemoryManager::getStatus() {
    String status = "=== RTC Memory Manager Status ===\n";
    status += "Total lecturas: " + String(totalReadings) + "\n";
    status += "Índice actual: " + String(currentIndex) + "\n";
    status += "Secuencia: " + String(rtc_data.sequence_number) + "\n";
    status += "Inicializado: " + String(isInitialized() ? "Sí" : "No") + "\n";
    status += "Tamaño estructura: " + String(sizeof(RTCDataStructure)) + " bytes\n";
    status += "SOLO DATOS - Sin logging de errores\n";
    status += "================================";
    
    return status;
}

// Obtener uso de memoria
/**
 * @brief Devuelve información sobre el uso actual de la memoria RTC.
 * 
 * @return String Informe de uso de memoria.
 */
String RTCMemoryManager::getMemoryUsage() {
    String usage = "=== Memory Usage ===\n";
    usage += "Estructura RTC: " + String(sizeof(RTCDataStructure)) + " bytes\n";
    usage += "Buffer lecturas: " + String(sizeof(rtc_data.readings)) + " bytes\n";
    usage += "Solo datos de sensores - sin buffers de errores\n";
    
    // Calcular uso actual de buffer de lecturas
    int usedReadings = (totalReadings < MAX_READINGS) ? totalReadings : MAX_READINGS;
    
    usage += "Lecturas usadas: " + String(usedReadings) + "/" + String(MAX_READINGS) + "\n";
    usage += "===================";
    
    return usage;
}

// Habilitar/deshabilitar Serial
/**
 * @brief Habilita o deshabilita la salida de logs por Serial.
 * 
 * @param enable True para habilitar, False para deshabilitar.
 */
void RTCMemoryManager::enableSerial(bool enable) {
    _enableSerialOutput = enable;
}

// Configurar callback de logging
/**
 * @brief Configura un callback para redirigir los mensajes de logging.
 * 
 * @param callback Función de tipo LogCallback.
 */
void RTCMemoryManager::setLogCallback(LogCallback callback) {
    _logCallback = callback;
}

// Verificar si está inicializada
/**
 * @brief Verifica si la memoria RTC está correctamente inicializada.
 * 
 * @return true Si la memoria está inicializada.
 * @return false Si no lo está.
 */
bool RTCMemoryManager::isInitialized() {
    return (rtc_data.magic_start == MAGIC_START && rtc_data.magic_end == MAGIC_END);
}

// ——— MÉTODOS PRIVADOS ———

/**
 * @brief Calcula el CRC32 de un bloque de datos dado.
 * 
 * @param data Puntero al bloque de datos.
 * @param length Longitud del bloque de datos en bytes.
 * @return uint32_t Valor del CRC32 calculado.
 */
uint32_t RTCMemoryManager::calculateCRC32(const void* data, size_t length) {
    return esp_crc32_le(0xFFFFFFFF, (const uint8_t*)data, length) ^ 0xFFFFFFFF;
}

/**
 * @brief Actualiza los CRCs de cabecera y de datos en la estructura RTC.
 */
void RTCMemoryManager::updateCRCs() {
    rtc_data.header_crc = calculateCRC32(&rtc_data.sequence_number, sizeof(uint32_t) * 2);
    rtc_data.data_crc = calculateCRC32(&rtc_data.readings, sizeof(rtc_data.readings));
}

/**
 * @brief Valida los rangos lógicos de los índices y contadores en memoria.
 * 
 * @return true Si los rangos son válidos.
 * @return false Si se detecta alguna inconsistencia.
 */
bool RTCMemoryManager::validateLogicalRanges() {
    if (currentIndex < 0 || currentIndex >= MAX_READINGS) {
        logf(" currentIndex fuera de rango: %d", currentIndex);
        return false;
    }
    
    if (totalReadings > MAX_TOTAL_READINGS) {
        logf(" totalReadings sospechoso: %d", totalReadings);
        return false;
    }
    
    return true;
}

// Métodos privados de logging
/**
 * @brief Envía un mensaje de log simple ya sea por callback o por Serial.
 * 
 * @param message Cadena de texto a mostrar.
 */
void RTCMemoryManager::log(const char* message) {
    if (_logCallback) {
        _logCallback(message);
    } else if (_enableSerialOutput && Serial) {
        Serial.println(message);
    }
}

/**
 * @brief Envía un mensaje de log formateado usando printf-style.
 * 
 * @param format Cadena de formato (similar a printf).
 * @param ... Argumentos variables para completar el formato.
 */
void RTCMemoryManager::logf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    log(buffer);
}