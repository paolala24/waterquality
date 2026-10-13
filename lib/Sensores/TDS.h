/**
 * @file TDS.h
 * @brief Definición del módulo de sensor TDS (Total Dissolved Solids) para ESP32
 * @details Este header contiene todas las definiciones, estructuras, constantes y
 *          prototipos de funciones para el manejo del sensor analógico TDS conectado
 *          al ADC del ESP32. Implementa compensación por temperatura, conversión
 *          EC→TDS y algoritmo polinómico cúbico GravityTDS.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#ifndef TDS_SENSOR_H
#define TDS_SENSOR_H

#include <Arduino.h>
#include <esp_adc_cal.h>

// ——— Configuración del sensor TDS ———

/**
 * @def TDS_SENSOR_PIN
 * @brief Pin GPIO del ADC para conexión del sensor TDS
 * @details Debe ser un pin compatible con ADC1 del ESP32 (GPIO 32-39).
 *          GPIO34 es típicamente usado en módulos de desarrollo.
 * @note No usar pines de ADC2 si WiFi está activo (conflicto de hardware).
 */
#define TDS_SENSOR_PIN          34    

/**
 * @def TDS_OPERATION_TIMEOUT
 * @brief Timeout máximo para operación completa de lectura en milisegundos
 * @details Si la función takeReadingWithTimeout() excede este tiempo, retorna
 *          error de timeout (TDS_STATUS_TIMEOUT) y registra el evento.
 * @note 10000 ms permite completar muestreo y cálculos sin bloquear indefinidamente.
 */
#define TDS_OPERATION_TIMEOUT   10000  // Timeout para operación del sensor

// ——— VALORES CALIBRADOS FIJOS ———

/**
 * @def TDS_CALIBRATED_KVALUE
 * @brief Factor de calibración de la celda TDS (kValue) por defecto
 * @details Coeficiente multiplicador que ajusta la conductividad eléctrica (EC)
 *          calculada según características específicas de la celda/electrodo usado.
 *          Este valor debe calibrarse con solución estándar conocida (ej: 1413 µS/cm).
 * @warning VALOR CRÍTICO: Debe ajustarse según el sensor físico específico.
 *          Valores típicos: 1.0 (sin ajuste) a 2.0 (celdas de alta sensibilidad).
 * @note Unidad: Adimensional (factor multiplicador)
 */
#define TDS_CALIBRATED_KVALUE   1.60f     

/**
 * @def TDS_CALIBRATED_VOFFSET
 * @brief Offset de voltaje calibrado para compensar errores del ADC/sensor
 * @details Valor restado al voltaje medido para corregir desviaciones sistemáticas
 *          del ADC o offset del amplificador del sensor. Debe calibrarse midiendo
 *          voltaje en agua destilada (TDS ≈ 0).
 * @warning Si voltaje calibrado resulta negativo, este offset es demasiado alto.
 *          Usar debugVoltageReading() para diagnosticar y ajustar.
 * @note Unidad: Voltios (V). Típicamente entre 0.0V y 0.2V.
 */
#define TDS_CALIBRATED_VOFFSET  0.10000f 

// ——— Estructura de datos del sensor TDS ———

/**
 * @struct TDSReading
 * @brief Estructura empaquetada que representa una lectura completa del sensor TDS
 * @details Contiene todos los datos relevantes de una medición: valor TDS, conductividad
 *          eléctrica (EC), temperatura usada para compensación, timestamp, estado de
 *          validez y códigos de error. Empaquetada con __attribute__((packed)) para
 *          optimizar memoria en almacenamiento persistente (RTC Memory, EEPROM, etc.).
 * 
 * @note Tamaño aproximado: 21 bytes (packed)
 * @note EC (µS/cm) y TDS (ppm) están relacionados por: TDS = EC × 0.5
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;         // Tiempo en milisegundos
    float tds_value;           // Valor TDS en ppm
    float ec_value;            // Conductividad eléctrica en µS/cm
    float temperature;         // Temperatura usada para compensación
    uint16_t reading_number;   // Número de lectura
    uint8_t sensor_status;     // Estado del sensor (flags)
    bool valid;                // Indica si la lectura es válida
} TDSReading;

// ——— Códigos de estado del sensor ———

/**
 * @def TDS_STATUS_OK
 * @brief Código de estado: lectura exitosa sin errores
 * @details Todos los bits en 0. Indica que la lectura se completó correctamente,
 *          voltaje, TDS y EC están dentro de rangos válidos.
 */
#define TDS_STATUS_OK               0x00  // Sin errores

/**
 * @def TDS_STATUS_TIMEOUT
 * @brief Flag de timeout: operación de lectura excedió TDS_OPERATION_TIMEOUT
 * @details Bit 0. Indica que takeReadingWithTimeout() no pudo completarse en el
 *          tiempo asignado. Posibles causas: ADC bloqueado, hardware desconectado.
 */
#define TDS_STATUS_TIMEOUT          0x01  // Flag de timeout

/**
 * @def TDS_STATUS_INVALID_READING
 * @brief Flag de lectura inválida: datos inconsistentes o sensor no inicializado
 * @details Bit 1. Indica problemas genéricos en la lectura: sensor no inicializado,
 *          valores NaN, TDS/EC fuera de rango, o errores de comunicación con hardware.
 */
#define TDS_STATUS_INVALID_READING  0x02  // Flag de lectura inválida

/**
 * @def TDS_STATUS_VOLTAGE_LOW
 * @brief Flag de voltaje bajo: voltaje medido < MIN_VALID_VOLTAGE
 * @details Bit 2. Indica que el voltaje está por debajo del rango válido (< 0.001V).
 *          Posibles causas: sensor desconectado, circuito abierto, alimentación insuficiente.
 */
#define TDS_STATUS_VOLTAGE_LOW      0x04  // Voltaje muy bajo

/**
 * @def TDS_STATUS_VOLTAGE_HIGH
 * @brief Flag de voltaje alto: voltaje medido > MAX_VALID_VOLTAGE
 * @details Bit 3. Indica que el voltaje excede el rango válido (> 2.2V).
 *          Posibles causas: cortocircuito, sensor dañado, amplificador saturado.
 * @note Sensor Gravity TDS opera típicamente entre 0-2.0V, límite 2.2V es margen de seguridad.
 */
#define TDS_STATUS_VOLTAGE_HIGH     0x08  // Voltaje muy alto

// ——— Namespace para el sensor TDS ———

/**
 * @namespace TDSSensor
 * @brief Espacio de nombres para todas las funcionalidades del sensor TDS
 * @details Encapsula variables, constantes y funciones relacionadas con el sensor TDS.
 *          Evita contaminación del namespace global y facilita integración modular.
 */
namespace TDSSensor {
    
    // Constantes de validación

    /**
     * @brief Valor mínimo válido de TDS en ppm
     * @details Límite inferior del rango de medición. TDS negativo indica error de calibración.
     */
    constexpr float MIN_VALID_TDS = 0.0;

    /**
     * @brief Valor máximo válido de TDS en ppm
     * @details Límite superior del rango de medición del sensor Gravity TDS.
     *          Valores por encima (>2000 ppm) exceden capacidad del sensor o indican error.
     * @note 2000 ppm equivale aproximadamente a 4000 µS/cm de EC.
     */
    constexpr float MAX_VALID_TDS = 2000.0;

    /**
     * @brief Valor mínimo válido de EC en µS/cm
     * @details Límite inferior del rango de conductividad eléctrica. EC negativo indica error.
     */
    constexpr float MIN_VALID_EC = 0.0;

    /**
     * @brief Valor máximo válido de EC en µS/cm
     * @details Límite superior del rango de medición del sensor Gravity TDS.
     *          Valores por encima (>4000 µS/cm) exceden capacidad del sensor.
     * @note 4000 µS/cm equivale aproximadamente a 2000 ppm de TDS con factor 0.5.
     */
    constexpr float MAX_VALID_EC = 4000.0;

    /**
     * @brief Voltaje mínimo válido del sensor en voltios
     * @details Voltajes por debajo (< 0.001V) sugieren sensor desconectado o circuito abierto.
     *          Margen muy bajo para detectar lecturas espurias cerca de 0V.
     */
    constexpr float MIN_VALID_VOLTAGE = 0.001;

    /**
     * @brief Voltaje máximo válido del sensor en voltios
     * @details Voltajes por encima (> 2.2V) sugieren sensor saturado o hardware dañado.
     *          Sensor Gravity TDS opera típicamente entre 0-2.0V, límite 2.2V es margen de seguridad.
     * @note Configuración ADC con atenuación 6dB soporta hasta 2.2V.
     */
    constexpr float MAX_VALID_VOLTAGE = 2.2;
    
    // ——— Valores de calibración fijos ———

    /**
     * @brief Factor de calibración de la celda TDS (kValue) actual
     * @details Variable externa modificable en tiempo de ejecución mediante setCalibration().
     *          Inicializada con TDS_CALIBRATED_KVALUE del header.
     * @note Unidad: Adimensional (factor multiplicador)
     */
    extern float kValue;    
    
    /**
     * @brief Offset de voltaje para compensar errores del ADC/sensor
     * @details Variable externa modificable en tiempo de ejecución mediante setCalibration().
     *          Inicializada con TDS_CALIBRATED_VOFFSET del header.
     * @note Unidad: Voltios (V)
     */
    extern float voltageOffset;      
    
    // ——— Funciones principales ———

    /**
     * @brief Inicializa el sensor TDS en el pin ADC especificado
     * @details Configura el ADC con:
     *          - Resolución: 12 bits (0-4095)
     *          - Atenuación: 6dB (rango 0-2.2V, apropiado para sensor TDS hasta 2.0V)
     *          - Calibración específica del chip ESP32
     *          Es seguro llamar múltiples veces (verifica estado de inicialización).
     * @param pin Pin GPIO compatible con ADC1 del ESP32 (por defecto TDS_SENSOR_PIN)
     * @return true si inicialización exitosa o ya estaba inicializado
     * @note Requiere llamarse una vez en setup() antes de usar otras funciones.
     * @warning Discrepancia entre ADC_6db y ADC_ATTEN_DB_6 + ADC_WIDTH_BIT_13.
     *          PENDIENTE: Verificar consistencia (similar a sensor pH).
     */
    bool initialize(uint8_t pin = TDS_SENSOR_PIN);

    /**
     * @brief Realiza una lectura completa de TDS (wrapper de takeReadingWithTimeout)
     * @details Función de conveniencia que llama internamente a takeReadingWithTimeout().
     * @param temperature Temperatura actual del agua en °C (requerida para compensación precisa)
     * @return Estructura TDSReading con resultado completo de la medición
     * @see takeReadingWithTimeout() para detalles completos del proceso de lectura
     */
    TDSReading takeReading(float temperature);

    /**
     * @brief Realiza lectura completa de TDS con control de timeout y validación exhaustiva
     * @details Proceso completo:
     *          1. Verifica inicialización del sensor
     *          2. Incrementa contador global de lecturas
     *          3. Lee voltaje calibrado (30 muestras promediadas con offset aplicado)
     *          4. Verifica timeout de operación (< TDS_OPERATION_TIMEOUT)
     *          5. Valida rango de voltaje (0.001V - 2.2V)
     *          6. Compensa voltaje por temperatura usando coeficiente 2%/°C
     *          7. Calcula EC usando polinomio cúbico GravityTDS × kValue
     *          8. Convierte EC a TDS usando factor 0.5
     *          9. Valida rangos de TDS (0-2000 ppm) y EC (0-4000 µS/cm)
     *          10. Actualiza last_reading y registra errores si corresponde
     * @param temperature Temperatura del agua en °C (requerida para compensación precisa)
     * @return Estructura TDSReading con campos:
     *         - tds_value: TDS en ppm (0.0 si inválido)
     *         - ec_value: EC en µS/cm (0.0 si inválido)
     *         - temperature: Temperatura usada en la compensación
     *         - timestamp: millis() al momento de la lectura
     *         - reading_number: Número secuencial de lectura
     *         - valid: true si lectura válida y dentro de todos los rangos
     *         - sensor_status: Código bit-field de estado (ver TDS_STATUS_*)
     * @note Si hay timeout o valores fuera de rango, decrementa el contador global.
     * @warning Función bloqueante por ~30ms (tiempo de muestreo del ADC).
     */
    TDSReading takeReadingWithTimeout(float temperature);
    
    // ——— Funciones de calibración ———

    /**
     * @brief Establece nuevos parámetros de calibración manualmente
     * @details Actualiza kValue (factor de celda) y voltageOffset (offset ADC).
     *          Útil después de calibración externa con solución estándar conocida.
     * @param kVal Nuevo factor de calibración de celda (típicamente 1.0 - 2.0)
     * @param vOffset Nuevo offset de voltaje en voltios (típicamente 0.0 - 0.2V)
     * @note Imprime confirmación de cambios en Serial para verificación.
     */
    void setCalibration(float kVal, float vOffset);

    /**
     * @brief Obtiene los parámetros de calibración actuales por referencia
     * @details Permite al sistema principal consultar calibración sin modificarla.
     *          Útil para guardar configuración en memoria persistente (EEPROM, RTC Memory).
     * @param[out] kVal Referencia donde se almacenará el kValue actual
     * @param[out] vOffset Referencia donde se almacenará el voltageOffset actual
     */
    void getCalibration(float& kVal, float& vOffset);

    /**
     * @brief Restablece la calibración a valores por defecto del header
     * @details Restaura kValue y voltageOffset a TDS_CALIBRATED_KVALUE y
     *          TDS_CALIBRATED_VOFFSET. Útil para resetear calibraciones incorrectas
     *          o volver a estado de fábrica.
     * @note Imprime confirmación en Serial.
     */
    void resetToDefaultCalibration();
    
    // ——— Funciones de estado ———

    /**
     * @brief Consulta si el sensor está inicializado y listo para uso
     * @return true si initialize() fue llamado exitosamente
     */
    bool isInitialized();

    /**
     * @brief Consulta si la última lectura almacenada es válida
     * @return true si last_reading.valid es true
     */
    bool isLastReadingValid();

    /**
     * @brief Obtiene el valor TDS de la última lectura almacenada
     * @return TDS en ppm (0.0 si última lectura fue inválida)
     */
    float getLastTDS();

    /**
     * @brief Obtiene la conductividad eléctrica de la última lectura almacenada
     * @return EC en µS/cm (0.0 si última lectura fue inválida)
     */
    float getLastEC();

    /**
     * @brief Obtiene timestamp de la última lectura válida realizada
     * @return millis() del momento de última lectura exitosa
     */
    uint32_t getLastReadingTime();

    /**
     * @brief Obtiene el total de lecturas realizadas desde el contador global
     * @return Número total de lecturas o 0 si contador no está vinculado
     */
    uint16_t getTotalReadings();
    
    // ——— Funciones de utilidad ———

    /**
     * @brief Imprime por Serial la última lectura almacenada en formato estructurado
     * @details Muestra: número de lectura, TDS, EC, temperatura, timestamp, estado.
     *          Útil para depuración y monitoreo en tiempo real.
     * @note Si no hay lecturas previas (reading_number == 0), informa al usuario.
     */
    void printLastReading();

    /**
     * @brief Valida si un valor TDS está dentro del rango aceptable
     * @param tds Valor TDS a validar en ppm
     * @return true si TDS está entre MIN_VALID_TDS (0.0) y MAX_VALID_TDS (2000.0) y no es NaN
     */
    bool isTDSInRange(float tds);

    /**
     * @brief Valida si un valor EC está dentro del rango aceptable
     * @param ec Valor EC a validar en µS/cm
     * @return true si EC está entre MIN_VALID_EC (0.0) y MAX_VALID_EC (4000.0) y no es NaN
     */
    bool isECInRange(float ec);

    /**
     * @brief Valida si un voltaje está dentro del rango aceptable del sensor
     * @param voltage Voltaje en voltios a validar
     * @return true si voltaje está entre MIN_VALID_VOLTAGE (0.001V) y MAX_VALID_VOLTAGE (2.2V) y no es NaN
     */
    bool isVoltageInRange(float voltage);

    /**
     * @brief Clasifica la calidad del agua según su TDS
     * @param tds Valor TDS a clasificar en ppm
     * @return String descriptivo de la calidad del agua:
     *         - "Muy pura" (TDS < 50 ppm) - Agua destilada/osmosis inversa
     *         - "Excelente" (50 ≤ TDS < 150 ppm) - Agua embotellada premium
     *         - "Buena" (150 ≤ TDS < 300 ppm) - Agua potable de calidad
     *         - "Aceptable" (300 ≤ TDS < 500 ppm) - Agua potable estándar
     *         - "Pobre" (500 ≤ TDS < 900 ppm) - Calidad marginal
     *         - "Muy pobre" (TDS ≥ 900 ppm) - No recomendada para consumo
     * @note Clasificación según estándares EPA y OMS para agua potable.
     */
    String getWaterQuality(float tds);
    
    // ——— Funciones para integración con sistema principal ———

    /**
     * @brief Vincula el sensor con un contador global de lecturas del sistema
     * @details Permite que el módulo TDS incremente automáticamente un contador externo
     *          en cada lectura válida. Útil para estadísticas globales del sistema.
     * @param total_readings_ptr Puntero a uint16_t que será incrementado en cada lectura válida
     * @note El puntero debe apuntar a memoria válida durante toda la vida útil del sensor.
     * @warning No pasar punteros a variables locales que puedan salir de scope.
     */
    void setReadingCounter(uint16_t* total_readings_ptr);

    /**
     * @brief Vincula el sensor con un sistema de logging de errores externo
     * @details Permite que el módulo TDS reporte errores (timeout, lectura inválida, etc.)
     *          a un sistema centralizado de gestión de errores o logger.
     * @param log_error_func Puntero a función con firma: void(int code, int severity, uint32_t context)
     *        - code: Código de error (1=timeout, 2=lectura inválida, etc.)
     *        - severity: Nivel de severidad (1=warning, 2=error, 3=crítico, etc.)
     *        - context: Información contextual (tiempo transcurrido, voltaje*1000, TDS, etc.)
     * @note La función debe ser thread-safe si se usa en entorno multitarea (FreeRTOS).
     * @warning No pasar punteros a funciones lambda sin captura estática.
     */
    void setErrorLogger(void (*log_error_func)(int, int, uint32_t));
    
    // ——— Variables internas del módulo ———

    /**
     * @brief Bandera de estado de inicialización del sensor TDS
     * @details Indica si initialize() fue llamado exitosamente. Evita operaciones sobre
     *          hardware no configurado.
     */
    extern bool initialized;

    /**
     * @brief Pin GPIO asignado al ADC para lectura del sensor TDS
     * @details Configurado en initialize(). Debe ser pin compatible con ADC1.
     */
    extern uint8_t sensor_pin;

    /**
     * @brief Timestamp de la última lectura válida realizada
     * @details Almacena millis() del momento de última lectura exitosa. Útil para
     *          calcular intervalos entre mediciones o detectar fallas prolongadas.
     */
    extern uint32_t last_reading_time;

    /**
     * @brief Última estructura de lectura capturada por el sensor
     * @details Contiene resultado completo de última llamada a takeReadingWithTimeout().
     *          Accesible mediante getLastTDS(), getLastEC(), etc.
     */
    extern TDSReading last_reading;

    /**
     * @brief Puntero al contador global de lecturas del sistema
     * @details Configurado mediante setReadingCounter(). nullptr si no está vinculado.
     */
    extern uint16_t* total_readings_counter;

    /**
     * @brief Puntero a función de logging de errores del sistema
     * @details Configurado mediante setErrorLogger(). nullptr si no está vinculado.
     */
    extern void (*error_logger)(int code, int severity, uint32_t context);
    
    /**
     * @brief Características de calibración del ADC del ESP32
     * @details Estructura que almacena parámetros de calibración específicos del chip
     *          para conversión precisa de valores crudos ADC a voltajes reales (mV).
     *          Inicializada en initialize() con esp_adc_cal_characterize().
     */
    extern esp_adc_cal_characteristics_t adc_chars;
    
    // ——— Configuración de muestreo ———

    /**
     * @brief Número de lecturas del ADC a promediar en cada medición
     * @details Constante que define cuántas muestras consecutivas se toman y promedian
     *          para reducir ruido eléctrico y obtener mediciones más estables.
     *          Cada muestra tiene 1ms de delay, por lo que 30 muestras = ~30ms bloqueado.
     * @note Mayor cantidad de muestras → mayor estabilidad pero mayor tiempo de lectura.
     *       Balancear según requisitos de tiempo real del sistema.
     */
    constexpr int SAMPLES = 30;  // Número de lecturas a promediar
    
    // ——— Funciones adicionales para debugging ———

    /**
     * @brief Muestra información completa de calibración y estado del sensor por Serial
     * @details Imprime:
     *          - Estado de inicialización (inicializado / no inicializado)
     *          - Pin ADC configurado
     *          - kValue (factor de calibración de celda)
     *          - voltageOffset (offset de voltaje ADC)
     *          - TDS_FACTOR (relación EC→TDS, típicamente 0.5)
     *          - Coeficientes del polinomio cúbico (A3, A2, A1)
     *          - Información de última lectura válida si existe
     * @note Útil para verificación rápida de configuración y diagnóstico de problemas.
     */
    void showCalibrationInfo();

    /**
     * @brief Realiza una lectura de prueba y muestra resultados detallados paso a paso
     * @details Lee voltaje calibrado, compensa por temperatura (asumiendo 25°C), calcula
     *          EC y TDS, y muestra cada etapa del proceso. No actualiza last_reading ni
     *          contadores globales. Ideal para verificación rápida sin afectar estadísticas.
     * @note Requiere sensor inicializado. Función bloqueante por ~30ms.
     * @note Usa temperatura fija de 25°C (sin compensación) para simplificar debug.
     */
    void testReading();

    /**
     * @brief Función de debug para verificar voltaje crudo y offset aplicado
     * @details Toma muestras del ADC sin aplicar offset y muestra:
     *          - Voltaje crudo (antes de restar offset)
     *          - Offset actual configurado
     *          - Voltaje final (crudo - offset)
     *          - Sugerencia de nuevo offset si el voltaje final es negativo
     * @note Útil para diagnosticar problemas de calibración donde voltaje < 0.
     * @note No actualiza last_reading ni contadores. Solo para depuración.
     * @warning Requiere sensor inicializado. Función bloqueante por ~30ms.
     */
    void debugVoltageReading();
}

#endif // TDS_SENSOR_H