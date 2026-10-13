/**
 * @file CalibrationManager.cpp
 * @brief Implementación de CalibrationManager: gestión, validación, persistencia y aplicación
 *        de parámetros de calibración para sensores de pH, TDS y turbidez en ESP32.
 *
 * @details
 * Este módulo centraliza la lógica para mantener una única fuente de verdad (single source of truth)
 * de calibraciones en el dispositivo. Almacena la estructura en RTC memory (persistente entre deep-sleeps),
 * valida integridad mediante CRC32, permite restaurar valores por defecto, procesar comandos JSON
 * de calibración, exportar la configuración a JSON y aplicar los parámetros a los controladores
 * de sensor del sistema (pHSensor, TDSSensor, TurbiditySensor).
 *
 * Características principales:
 *  - Uso de estructura empaquetada en RTC_DATA_ATTR para persistencia.
 *  - CRC32 (esp_crc32_le) para protección frente a corrupción de datos.
 *  - Validación de rangos por sensor para evitar configuraciones inválidas.
 *  - API pública para obtener/establecer parámetros y serializar a JSON.
 *  - Logging flexible: Serial o callback externo.
 *
 * @see CalibrationManager.h
 * @author Daniel Acosta - Santiago Erazo
 * @date 2025-11-18
 */
#include "CalibrationManager.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include "pH.h"
#include "TDS.h"
#include "Turbidez.h"

// Variable en RTC Memory

/**
 * @brief Estructura de calibración persistente en RTC memory.
 * @details Declarada con RTC_DATA_ATTR para que sobreviva a ciclos de deep sleep/reboot parcial.
 */
RTC_DATA_ATTR CalibrationManager::CalibrationData rtc_calibration_data;

/**
 * @brief Puntero a la estructura de calibración utilizada por la clase.
 * @details Inicializado apuntando a rtc_calibration_data. Se usa de forma estática para permitir
 * el acceso desde cualquier instancia del manager.
 */
CalibrationManager::CalibrationData* CalibrationManager::_calibData = &rtc_calibration_data;

/**
 * @brief Constructor.
 * @param enableSerial Habilita la salida por Serial (true por defecto).
 */
CalibrationManager::CalibrationManager(bool enableSerial)
    : _enableSerialOutput(enableSerial), _initialized(false), _logCallback(nullptr) {
}

/**
 * @brief Inicializa el módulo de calibración.
 * @details
 * - Arranca Serial si está habilitado.
 * - Valida integridad de los datos en RTC (CRC + validación de rangos).
 * - Si los datos son inválidos restaura valores por defecto.
 * - Aplica la calibración vigente a los sensores inicializados.
 *
 * @return true si el proceso de inicialización terminó (siempre devuelve true en la implementación actual).
 */
bool CalibrationManager::begin() {
    if (_enableSerialOutput && !Serial) {
        Serial.begin(115200);
        delay(100);
    }
    
    log("=== Calibration Manager Inicializado ===");
    
    if (!validateIntegrity()) {
        log("⚠ Datos inválidos - Restaurando valores por defecto");
        restoreDefaults();
    } else {
        log("✓ Datos de calibración válidos");
        logf("  Última actualización: %u", _calibData->last_update);
        logf("  Actualizaciones: %u", _calibData->update_count);
    }
    
    _initialized = true;
    applyToSensors();
    
    return true;
}

/**
 * @brief Valida la integridad de la estructura de calibración.
 * @details
 * - Calcula CRC sobre la estructura (sin contar el campo crc).
 * - Compara CRC calculado vs almacenado.
 * - Valida rangos de pH, TDS y turbidez mediante funciones específicas de cada sensor.
 *
 * @return true si CRC y validaciones de rango son correctas.
 */
bool CalibrationManager::validateIntegrity() {
    size_t dataSize = sizeof(CalibrationData) - sizeof(uint32_t);
    uint32_t calculatedCRC = calculateCRC32(_calibData, dataSize);
    
    if (_calibData->crc != calculatedCRC) {
        return false;
    }
    
    return validatePHValues(_calibData->ph_offset, _calibData->ph_slope) &&
        validateTDSValues(_calibData->tds_kvalue, _calibData->tds_voffset) &&
        validateTurbidityValues(_calibData->turb_coeff_a, _calibData->turb_coeff_b,
                                _calibData->turb_coeff_c, _calibData->turb_coeff_d);
}

/**
 * @brief Restaura la calibración a valores por defecto definidos en el header.
 * @details Actualiza timestamp, contador de actualizaciones y recalcula CRC.
 */
void CalibrationManager::restoreDefaults() {
    _calibData->ph_offset = DEFAULT_PH_OFFSET;
    _calibData->ph_slope = DEFAULT_PH_SLOPE;
    _calibData->tds_kvalue = DEFAULT_TDS_KVALUE;
    _calibData->tds_voffset = DEFAULT_TDS_VOFFSET;
    _calibData->turb_coeff_a = DEFAULT_TURB_A;
    _calibData->turb_coeff_b = DEFAULT_TURB_B;
    _calibData->turb_coeff_c = DEFAULT_TURB_C;
    _calibData->turb_coeff_d = DEFAULT_TURB_D;
    _calibData->last_update = millis();
    _calibData->update_count = 0;
    updateCRC();
}

// Implementar getters

/**
 * @brief Devuelve el offset actual del sensor pH almacenado.
 * @return Valor offset pH.
 */
float CalibrationManager::getPHOffset() { return _calibData->ph_offset; }

/**
 * @brief Devuelve la pendiente (slope) del sensor pH.
 * @return Valor slope pH.
 */
float CalibrationManager::getPHSlope() { return _calibData->ph_slope; }

/**
 * @brief Devuelve el valor K del sensor TDS.
 * @return Valor K de TDS.
 */
float CalibrationManager::getTDSKValue() { return _calibData->tds_kvalue; }

/**
 * @brief Devuelve el offset de voltaje del sensor TDS.
 * @return Offset de voltaje de TDS.
 */
float CalibrationManager::getTDSVOffset() { return _calibData->tds_voffset; }

/**
 * @brief Proporciona los coeficientes del polinomio de turbidez por referencia.
 * @param[out] a Coeficiente A.
 * @param[out] b Coeficiente B.
 * @param[out] c Coeficiente C.
 * @param[out] d Coeficiente D.
 */
void CalibrationManager::getTurbidityCoefficients(float& a, float& b, float& c, float& d) {
    a = _calibData->turb_coeff_a;
    b = _calibData->turb_coeff_b;
    c = _calibData->turb_coeff_c;
    d = _calibData->turb_coeff_d;
}

// Setters con validación

/**
 * @brief Establece parámetros de calibración para el sensor pH.
 * @details Valida estado inicializado, validez de los parámetros y actualiza CRC.
 *
 * @param offset Nuevo offset de pH.
 * @param slope Nueva pendiente del pH.
 * @return CalibrationResult indicando éxito o tipo de error.
 */
CalibrationManager::CalibrationResult CalibrationManager::setPHCalibration(float offset, float slope) {
    if (!_initialized) return CALIB_ERROR_NOT_INITIALIZED;
    if (!validatePHValues(offset, slope)) return CALIB_ERROR_OUT_OF_RANGE;
    
    _calibData->ph_offset = offset;
    _calibData->ph_slope = slope;
    _calibData->last_update = millis();
    _calibData->update_count++;
    updateCRC();
    
    logf("✓ pH calibrado: offset=%.2f, slope=%.2f", offset, slope);
    return CALIB_SUCCESS;
}


/**
 * @brief Establece parámetros de calibración para el sensor TDS.
 * @param kvalue Nuevo valor K.
 * @param voffset Nuevo offset de voltaje.
 * @return CalibrationResult indicando resultado.
 */
CalibrationManager::CalibrationResult CalibrationManager::setTDSCalibration(float kvalue, float voffset) {
    if (!_initialized) return CALIB_ERROR_NOT_INITIALIZED;
    if (!validateTDSValues(kvalue, voffset)) return CALIB_ERROR_OUT_OF_RANGE;
    
    _calibData->tds_kvalue = kvalue;
    _calibData->tds_voffset = voffset;
    _calibData->last_update = millis();
    _calibData->update_count++;
    updateCRC();
    
    logf("✓ TDS calibrado: k=%.6f, v=%.6f", kvalue, voffset);
    return CALIB_SUCCESS;
}

/**
 * @brief Establece los coeficientes del modelo polinomial para turbidez.
 * @param a Coeficiente A.
 * @param b Coeficiente B.
 * @param c Coeficiente C.
 * @param d Coeficiente D.
 * @return CalibrationResult indicando resultado.
 */
CalibrationManager::CalibrationResult CalibrationManager::setTurbidityCoefficients(
    float a, float b, float c, float d) {
    
    if (!_initialized) return CALIB_ERROR_NOT_INITIALIZED;
    if (!validateTurbidityValues(a, b, c, d)) return CALIB_ERROR_OUT_OF_RANGE;
    
    _calibData->turb_coeff_a = a;
    _calibData->turb_coeff_b = b;
    _calibData->turb_coeff_c = c;
    _calibData->turb_coeff_d = d;
    _calibData->last_update = millis();
    _calibData->update_count++;
    updateCRC();
    
    logf("✓ Turbidez calibrada: a=%.1f, b=%.1f, c=%.1f, d=%.1f", a, b, c, d);
    return CALIB_SUCCESS;
}

// Validaciones

/**
 * @brief Valida parámetros de pH.
 * @details Revisa NaN, rangos lógicos y magnitud mínima de slope para evitar divisiones/errores.
 *
 * @param offset Offset propuesto.
 * @param slope Slope propuesto.
 * @return true si válidos.
 */

bool CalibrationManager::validatePHValues(float offset, float slope) {
    if (isnan(offset) || isnan(slope)) return false;
    if (offset < -5.0f || offset > 5.0f) return false;
    if (slope < -10.0f || slope > 10.0f) return false;
    if (fabs(slope) < 0.1f) return false;
    return true;
}

/**
 * @brief Valida parámetros de TDS.
 * @param kvalue Valor K propuesto.
 * @param voffset Offset de voltaje propuesto.
 * @return true si válidos.
 */
bool CalibrationManager::validateTDSValues(float kvalue, float voffset) {
    if (isnan(kvalue) || isnan(voffset)) return false;
    if (kvalue < 0.1f || kvalue > 5.0f) return false;
    if (voffset < -1.0f || voffset > 1.0f) return false;
    return true;
}

/**
 * @brief Valida coeficientes del polinomio de turbidez.
 * @details Rechaza NaN, Inf y magnitudes extraordinarias que indicarían corrupción.
 *
 * @param a Coeficiente A.
 * @param b Coeficiente B.
 * @param c Coeficiente C.
 * @param d Coeficiente D.
 * @return true si los coeficientes están dentro de límites sensatos.
 */
bool CalibrationManager::validateTurbidityValues(float a, float b, float c, float d) {
    if (isnan(a) || isnan(b) || isnan(c) || isnan(d)) return false;
    if (isinf(a) || isinf(b) || isinf(c) || isinf(d)) return false;
    if (fabs(a) > 100000.0f || fabs(b) > 100000.0f || 
        fabs(c) > 100000.0f || fabs(d) > 100000.0f) return false;
    return true;
}

// Procesamiento de comandos JSON

/**
 * @brief Procesa un comando JSON para calibración.
 *
 * @details JSON esperado (ejemplo):
 * {
 *   "action": "calibrate",
 *   "ph_offset": 1.23,
 *   "ph_slope": 3.45,
 *   "tds_kvalue": 1.6,
 *   "tds_voffset": 0.1,
 *   "turb_coeff_a": -1120.4,
 *   "turb_coeff_b": 5742.3,
 *   "turb_coeff_c": -4352.9,
 *   "turb_coeff_d": -2500.0,
 *   "restore_defaults": false
 * }
 *
 * - Los campos son opcionales; si faltan se mantienen los valores actuales.
 * - Si se envía "restore_defaults": true, se restauran los valores por defecto.
 *
 * @param jsonCommand Cadena JSON con la instrucción.
 * @return CalibrationResult indicando éxito o tipo de error.
 */
CalibrationManager::CalibrationResult CalibrationManager::processCalibrationCommand(
    const String& jsonCommand) {
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonCommand);
    
    if (error) {
        logf("⚠ Error JSON: %s", error.c_str());
        return CALIB_ERROR_INVALID_VALUE;
    }
    
    if (!doc.containsKey("action") || doc["action"] != "calibrate") {
        return CALIB_ERROR_INVALID_VALUE;
    }
    
    log("📝 Procesando calibración...");
    
    CalibrationResult result = CALIB_SUCCESS;
    bool anyUpdated = false;
    
    // pH
    if (doc.containsKey("ph_offset") || doc.containsKey("ph_slope")) {
        float offset = doc.containsKey("ph_offset") ? 
                    doc["ph_offset"].as<float>() : _calibData->ph_offset;
        float slope = doc.containsKey("ph_slope") ? 
                    doc["ph_slope"].as<float>() : _calibData->ph_slope;
        
        result = setPHCalibration(offset, slope);
        if (result == CALIB_SUCCESS) {
            anyUpdated = true;
            pHSensor::setCalibration(offset, slope);
        }
    }
    
    // TDS
    if (doc.containsKey("tds_kvalue") || doc.containsKey("tds_voffset")) {
        float kvalue = doc.containsKey("tds_kvalue") ? 
                    doc["tds_kvalue"].as<float>() : _calibData->tds_kvalue;
        float voffset = doc.containsKey("tds_voffset") ? 
                    doc["tds_voffset"].as<float>() : _calibData->tds_voffset;
        
        result = setTDSCalibration(kvalue, voffset);
        if (result == CALIB_SUCCESS) {
            anyUpdated = true;
            TDSSensor::setCalibration(kvalue, voffset);
        }
    }
    
    // Turbidez
    if (doc.containsKey("turb_coeff_a") || doc.containsKey("turb_coeff_b") ||
        doc.containsKey("turb_coeff_c") || doc.containsKey("turb_coeff_d")) {
        
        float a = doc.containsKey("turb_coeff_a") ? 
                doc["turb_coeff_a"].as<float>() : _calibData->turb_coeff_a;
        float b = doc.containsKey("turb_coeff_b") ? 
                doc["turb_coeff_b"].as<float>() : _calibData->turb_coeff_b;
        float c = doc.containsKey("turb_coeff_c") ? 
                doc["turb_coeff_c"].as<float>() : _calibData->turb_coeff_c;
        float d = doc.containsKey("turb_coeff_d") ? 
                doc["turb_coeff_d"].as<float>() : _calibData->turb_coeff_d;
        
        result = setTurbidityCoefficients(a, b, c, d);
        if (result == CALIB_SUCCESS) {
            anyUpdated = true;
            TurbiditySensor::setCalibrationCoefficients(a, b, c, d);
        }
    }
    
    // Restaurar defaults
    if (doc.containsKey("restore_defaults") && doc["restore_defaults"].as<bool>()) {
        restoreDefaults();
        applyToSensors();
        anyUpdated = true;
    }
    
    if (anyUpdated) {
        log("✓ Calibración actualizada");
        printCalibrationInfo();
    }
    
    return result;
}

/**
 * @brief Serializa la configuración de calibración a JSON.
 * @return String conteniendo JSON con todos los parámetros y metadatos.
 */
String CalibrationManager::getCalibrationJSON() {
    StaticJsonDocument<512> doc;
    
    doc["ph_offset"] = _calibData->ph_offset;
    doc["ph_slope"] = _calibData->ph_slope;
    doc["tds_kvalue"] = _calibData->tds_kvalue;
    doc["tds_voffset"] = _calibData->tds_voffset;
    doc["turb_coeff_a"] = _calibData->turb_coeff_a;
    doc["turb_coeff_b"] = _calibData->turb_coeff_b;
    doc["turb_coeff_c"] = _calibData->turb_coeff_c;
    doc["turb_coeff_d"] = _calibData->turb_coeff_d;
    doc["last_update"] = _calibData->last_update;
    doc["update_count"] = _calibData->update_count;
    doc["crc"] = _calibData->crc;
    
    String output;
    serializeJson(doc, output);
    return output;
}

/**
 * @brief Imprime por Serial / callback la información completa de calibración.
 * @details Incluye parámetros por sensor, contador de actualizaciones y CRC.
 */
void CalibrationManager::printCalibrationInfo() {
    log("\n=== VALORES DE CALIBRACIÓN ===");
    logf("pH: offset=%.2f, slope=%.2f", _calibData->ph_offset, _calibData->ph_slope);
    logf("TDS: k=%.6f, v=%.6f", _calibData->tds_kvalue, _calibData->tds_voffset);
    logf("Turb: a=%.1f, b=%.1f, c=%.1f, d=%.1f", 
        _calibData->turb_coeff_a, _calibData->turb_coeff_b,
        _calibData->turb_coeff_c, _calibData->turb_coeff_d);
    logf("Updates: %u, CRC: 0x%08X", _calibData->update_count, _calibData->crc);
    log("==============================\n");
}


/**
 * @brief Obtiene timestamp de última actualización.
 * @return millis() almacenado al último cambio de calibración.
 */
uint32_t CalibrationManager::getLastUpdateTime() { return _calibData->last_update; }

/**
 * @brief Obtiene número de actualizaciones efectuadas.
 * @return Contador de actualizaciones.
 */
uint16_t CalibrationManager::getUpdateCount() { return _calibData->update_count; }


/**
 * @brief Aplica los parámetros almacenados a cada sensor si está inicializado.
 * @details Usa las APIs públicas de pHSensor, TDSSensor y TurbiditySensor.
 */
void CalibrationManager::applyToSensors() {
    log("🔧 Aplicando calibración a sensores...");
    
    if (pHSensor::isInitialized()) {
        pHSensor::setCalibration(_calibData->ph_offset, _calibData->ph_slope);
    }
    
    if (TDSSensor::isInitialized()) {
        TDSSensor::setCalibration(_calibData->tds_kvalue, _calibData->tds_voffset);
    }
    
    if (TurbiditySensor::isInitialized()) {
        TurbiditySensor::setCalibrationCoefficients(
            _calibData->turb_coeff_a, _calibData->turb_coeff_b,
            _calibData->turb_coeff_c, _calibData->turb_coeff_d
        );
    }
    
    log("✓ Calibración aplicada");
}


/**
 * @brief Registra una función callback para recibir logs.
 * @param callback Función con firma void(const char*).
 */
void CalibrationManager::setLogCallback(LogCallback callback) {
    _logCallback = callback;
}


/**
 * @brief Activa/desactiva la salida por Serial.
 * @param enable true para habilitar, false para deshabilitar.
 */
void CalibrationManager::enableSerial(bool enable) {
    _enableSerialOutput = enable;
}

/**
 * @brief Recalcula el CRC32 de la estructura de calibración y actualiza el campo correspondiente.
 * @note Calcula CRC sobre la estructura completa excepto el campo crc.
 */
void CalibrationManager::updateCRC() {
    size_t dataSize = sizeof(CalibrationData) - sizeof(uint32_t);
    _calibData->crc = calculateCRC32(_calibData, dataSize);
}


/**
 * @brief Calcula CRC32 little-endian usando la función nativa del ESP32.
 * @param data Puntero a datos.
 * @param length Longitud en bytes.
 * @return CRC32 calculado.
 */
uint32_t CalibrationManager::calculateCRC32(const void* data, size_t length) {
    return esp_crc32_le(0xFFFFFFFF, (const uint8_t*)data, length) ^ 0xFFFFFFFF;
}


/**
 * @brief Entrega un mensaje de log ya sea al callback registrado o a Serial si está habilitado.
 * @param message Texto plano a emitir.
 */
void CalibrationManager::log(const char* message) {
    if (_logCallback) {
        _logCallback(message);
    } else if (_enableSerialOutput && Serial) {
        Serial.println(message);
    }
}

/**
 * @brief Formatea y envía un log tipo printf.
 * @param format Formato estilo printf.
 * @param ... Argumentos variables.
 */
void CalibrationManager::logf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(buffer);
}