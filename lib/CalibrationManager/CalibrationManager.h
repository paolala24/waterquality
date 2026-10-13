/**
 * @file CalibrationManager.h
 * @brief Declaración de la clase CalibrationManager para la gestión centralizada de calibraciones de sensores.
 *
 * Este módulo administra todos los parámetros de calibración utilizados por los sensores del sistema:
 * **pH**, **TDS** y **Turbidez**, garantizando almacenamiento persistente, validación de rangos,
 * integridad mediante CRC32 y actualización controlada.
 *
 * La calibración es fundamental para asegurar la precisión del sistema de medición. Esta clase ofrece:
 *
 * - Almacenamiento persistente de parámetros mediante estructura empaquetada y CRC.
 * - Validación individual para cada sensor, evitando configuraciones corruptas o fuera de rango.
 * - Funciones seguras de actualización con retorno detallado de errores.
 * - Serialización a JSON para exportación remota o diagnóstico.
 * - Aplicación automática de parámetros a los controladores de sensores correspondientes.
 * - Sistema interno de logging configurable por Serial o callback externo.
 *
 * Esta clase actúa como **fuente única de verdad (single source of truth)** para todo el proceso
 * de calibración dentro del dispositivo.
 *
 * @author
 *  - Daniel Acosta  
 *  - Santiago Erazo  
 *
 * @version 2.0
 * @date 2025-11-18
 */

#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include "esp_crc.h"

/**
 * @class CalibrationManager
 * @brief Clase responsable de gestionar y validar los parámetros de
 * calibración de los sensores de pH, TDS y turbidez. Incluye mecanismos
 * de integridad, restauración por defecto, serialización JSON y registro
 * de eventos.
 */

class CalibrationManager {
public:

    /**
     * @struct CalibrationData
     * @brief Estructura compacta que almacena todos los parámetros de
     * calibración y metadatos asociados.
     *
     * Los valores se almacenan en formato empaquetado (`packed`) para
     * asegurar la consistencia en memoria RTC. Incluye:
     *  - Parámetros de calibración del sensor de pH.
     *  - Parámetros de calibración del sensor TDS.
     *  - Coeficientes polinomiales del sensor de turbidez.
     *  - Timestamps y contador de actualizaciones.
     *  - CRC32 para validar la integridad de los datos.
     */

    typedef struct __attribute__((packed)) {
        // --- Parámetros Sensor pH --- 
        float ph_offset; /**< Offset del sensor de pH */
        float ph_slope; /**< Pendiente o sensibilidad del sensor pH */
        
        // Parámetros Sensor TDS 
        float tds_kvalue; /**< Valor K de calibración para el sensor TDS */
        float tds_voffset;  /**< Offset de voltaje para el cálculo TDS */
        
        // Parámetros  Turbidity Sensor
        float turb_coeff_a; /**< Coeficiente A del polinomio de turbidez */
        float turb_coeff_b; /**< Coeficiente B del polinomio de turbidez */
        float turb_coeff_c; /**< Coeficiente C del polinomio de turbidez */
        float turb_coeff_d; /**< Coeficiente D del polinomio de turbidez */
        
        // Metadata
        uint32_t last_update; /**< Timestamp de la última calibración */
        uint16_t update_count; /**< Número total de actualizaciones realizadas */
        uint32_t crc; /**< CRC32 para verificar integridad */
    } CalibrationData;

    // Valores por defecto
    static constexpr float DEFAULT_PH_OFFSET = 1.33f;
    static constexpr float DEFAULT_PH_SLOPE = 3.5f;
    static constexpr float DEFAULT_TDS_KVALUE = 1.60f;
    static constexpr float DEFAULT_TDS_VOFFSET = 0.10000f;
    static constexpr float DEFAULT_TURB_A = -1120.4f;
    static constexpr float DEFAULT_TURB_B = 5742.3f;
    static constexpr float DEFAULT_TURB_C = -4352.9f;
    static constexpr float DEFAULT_TURB_D = -2500.0f;

    /**
     * @enum CalibrationResult
     * @brief Resultados posibles al realizar operaciones de calibración.
     */

    enum CalibrationResult {
        CALIB_SUCCESS = 0, /**< Operación completada sin errores */
        CALIB_ERROR_INVALID_VALUE, /**< Valores inválidos o NaN */
        CALIB_ERROR_OUT_OF_RANGE, /**< Valores fuera de rangos permitidos */
        CALIB_ERROR_CRC_MISMATCH, /**< Fallo de integridad por CRC incorrecto */
        CALIB_ERROR_WRITE_FAILED, /**< Error al intentar escribir valores */
        CALIB_ERROR_NOT_INITIALIZED /**< El sistema no ha sido inicializado */
    };

    /**
     * @typedef LogCallback
     * @brief Función callback para enviar mensajes de registro.
     */

    typedef void (*LogCallback)(const char* message);

private:
    bool _enableSerialOutput; /**< Habilita o deshabilita la salida por Serial */
    bool _initialized; /**< Indica si el módulo ha sido inicializado */
    LogCallback _logCallback; /**< Callback personalizado para registro */
    static CalibrationData* _calibData; /**< Puntero a la estructura global de calibración */

public:

    /**
     * @brief Constructor principal del gestor de calibración.
     * @param enableSerial Indica si se habilita la salida serial por defecto.
     */
    CalibrationManager(bool enableSerial = true);

    /**
     * @brief Inicializa la estructura de calibración y valida integridad.
     * @return true si la estructura es válida o se restauró correctamente.
     */
    bool begin();

    /**
     * @brief Verifica el CRC de los datos para validar integridad.
     * @return true si el CRC coincide, false si hay corrupción.
     */
    bool validateIntegrity();

    /**
     * @brief Restaura todos los valores de calibración a sus valores por defecto.
     */
    void restoreDefaults();
    
    // Getters/Setters pH

    /**
     * @brief Obtiene el offset del sensor pH.
     * @return Valor del offset almacenado.
     */
    float getPHOffset();

    /**
     * @brief Obtiene la pendiente del sensor pH.
     * @return Valor slope actual.
     */
    float getPHSlope();

    /**
     * @brief Establece nuevos parámetros de calibración para el sensor pH.
     * @param offset Nuevo valor de offset.
     * @param slope Nueva pendiente.
     * @return Resultado de la operación de calibración.
     */
    CalibrationResult setPHCalibration(float offset, float slope);
    
    // Getters/Setters TDS
    float getTDSKValue(); /**< Obtiene el valor K del sensor TDS */
    float getTDSVOffset(); /**< Obtiene el offset de voltaje del sensor TDS */

    /**
     * @brief Establece los parámetros de calibración del sensor TDS.
     * @param kvalue Nuevo valor K.
     * @param voffset Nuevo offset de voltaje.
     * @return Estado de la operación.
     */
    CalibrationResult setTDSCalibration(float kvalue, float voffset);
    
    // Getters/Setters Turbidez

    /**
     * @brief Obtiene todos los coeficientes del polinomio de turbidez.
     * @param a Coeficiente A.
     * @param b Coeficiente B.
     * @param c Coeficiente C.
     * @param d Coeficiente D.
     */
    void getTurbidityCoefficients(float& a, float& b, float& c, float& d);

    /**
     * @brief Establece nuevos coeficientes de turbidez.
     * @param a Nuevo coeficiente A.
     * @param b Nuevo coeficiente B.
     * @param c Nuevo coeficiente C.
     * @param d Nuevo coeficiente D.
     * @return Resultado de la calibración.
     */
    CalibrationResult setTurbidityCoefficients(float a, float b, float c, float d);
    
    // Validación
    bool validatePHValues(float offset, float slope); /**< Valida parámetros pH */
    bool validateTDSValues(float kvalue, float voffset); /**< Valida parámetros TDS */
    bool validateTurbidityValues(float a, float b, float c, float d); /**< Valida polinomio turbidez */
    
    // Procesamiento de comandos

    /**
     * @brief Procesa un comando JSON con parámetros de calibración.
     * @param jsonCommand Cadena JSON recibida.
     * @return Resultado de la asignación.
     */
    CalibrationResult processCalibrationCommand(const String& jsonCommand);
    
    /**
     * @brief Genera un JSON con toda la información de calibración actual.
     * @return Cadena JSON con parámetros y metadatos.
     */
    String getCalibrationJSON();
    
    // Información

    /**
     * @brief Imprime en Serial toda la información de calibración.
     */
    void printCalibrationInfo();
    uint32_t getLastUpdateTime(); /**< Devuelve fecha de última actualización */
    uint16_t getUpdateCount(); /**< Devuelve cantidad de actualizaciones */
    
    // Aplicar a sensores

    /**
     * @brief Aplica la calibración a todos los sensores del sistema.
     */
    void applyToSensors();
    
    /**
     * @brief Establece un callback para salida de logs personalizada.
     * @param callback Función externa para recibir mensajes.
     */
    void setLogCallback(LogCallback callback);

    /**
     * @brief Habilita o deshabilita salida serial.
     * @param enable Estado deseado.
     */
    void enableSerial(bool enable);

private:

/**
     * @brief Actualiza el CRC32 de la estructura de calibración.
     */
    void updateCRC();

    /**
     * @brief Calcula un CRC32 sobre una región de memoria.
     * @param data Puntero a los datos.
     * @param length Longitud en bytes.
     * @return Valor CRC32 calculado.
     */
    uint32_t calculateCRC32(const void* data, size_t length);

    /**
     * @brief Envía un mensaje de log al callback o Serial.
     * @param message Texto del mensaje.
     */
    void log(const char* message);

    /**
     * @brief Envía un mensaje de log formateado.
     * @param format Cadena de formato estilo printf.
     * @param ... Argumentos variables.
     */
    void logf(const char* format, ...);
};

#endif