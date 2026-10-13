/**
 * @file pH.h
 * @brief Definición del módulo de sensor de pH para ESP32
 * @details Este header contiene todas las definiciones, estructuras, constantes
 *          y prototipos de funciones para el manejo del sensor analógico de pH
 *          conectado al ADC del ESP32. Incluye calibración, validación, lectura
 *          con promediado estadístico y funciones de diagnóstico.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#ifndef PH_SENSOR_H
#define PH_SENSOR_H

#include <Arduino.h>
#include <esp_adc_cal.h>

// ——— Configuración del sensor de pH  ———

/**
 * @def PH_SENSOR_PIN
 * @brief Pin GPIO del ADC para conexión del sensor de pH
 * @details Debe ser un pin compatible con ADC1 del ESP32 (GPIO 32-39).
 *          GPIO33 es típicamente usado en módulos de desarrollo.
 * @note No usar pines de ADC2 si WiFi está activo (conflicto de hardware).
 */
#define PH_SENSOR_PIN           33    // Pin ADC

/**
 * @def PH_OPERATION_TIMEOUT
 * @brief Timeout máximo para operación completa de lectura en milisegundos
 * @details Si la función takeReadingWithTimeout() excede este tiempo, retorna
 *          error de timeout (PH_STATUS_TIMEOUT) y registra el evento.
 * @note 5000 ms permite completar múltiples muestras sin bloquear indefinidamente.
 */
#define PH_OPERATION_TIMEOUT    50000  // Timeout para operación del sensor

// ——— Valores de calibración por defecto ———

/**
 * @def PH_CALIBRATED_OFFSET
 * @brief Offset por defecto de la ecuación de calibración (intercepto 'b')
 * @details Valor calibrado en buffer pH 7.0 para este sensor específico.
 *          En la ecuación pH = slope * V + offset, este es el término independiente.
 * @warning Este valor depende del sensor físico. Debe recalibrarse si se cambia
 *          el sensor o amplificador analógico.
 * @note Unidad: pH (adimensional)
 */
#define PH_CALIBRATED_OFFSET    1.33f     // Offset calibrado en pH 7.0

/**
 * @def PH_CALIBRATED_SLOPE
 * @brief Pendiente por defecto de la ecuación de calibración (coeficiente 'm')
 * @details Define la sensibilidad del sensor (cambio de pH por voltio).
 *          En la ecuación pH = slope * V + offset, este es el coeficiente angular.
 * @warning Valor típico para sensores pH genéricos. Verificar con datasheet del
 *          sensor específico usado en el proyecto.
 * @note Unidad: pH/V (pH por voltio)
 */
#define PH_CALIBRATED_SLOPE     3.5f      // Pendiente de la curva de calibración

/**
 * @def PH_SAMPLING_INTERVAL
 * @brief Intervalo mínimo entre muestras individuales del ADC en milisegundos
 * @details Define el spacing temporal entre lecturas consecutivas del analogRead().
 *          Evita muestreo excesivamente rápido que podría introducir ruido.
 * @note Este valor NO define el intervalo total de lectura (ver PH_READ_INTERVAL_SECONDS).
 * @deprecated Preferir usar PH_MIN_SAMPLE_SPACING_MS definido en pH.cpp
 */
#define PH_SAMPLING_INTERVAL    20        // ms entre muestras

/**
 * @def PH_ARRAY_LENGTH
 * @brief Número de muestras a promediar en cada lectura completa
 * @details El algoritmo toma 40 muestras distribuidas temporalmente, descarta
 *          máximos y mínimos, y promedia el resto para reducir ruido y outliers.
 * @note Mayor cantidad de muestras → mayor tiempo de lectura pero mejor precisión.
 *       Balancear según requisitos de tiempo real del sistema.
 */
#define PH_ARRAY_LENGTH        40         // Número de muestras para promediar

// ——— Intervalo configurable de lectura (en segundos) ———
// 🔹 Aquí defines cada cuánto tiempo quieres que el sensor de pH lea datos. 
//    Solo cambia este valor para ajustar el tiempo en el futuro.

/**
 * @def PH_READ_INTERVAL_SECONDS
 * @brief Intervalo sugerido entre lecturas completas de pH en segundos
 * @details 🔹 LÍNEA CRÍTICA PARA CONFIGURACIÓN DE TIEMPO DE MUESTREO 🔹
 *          Define cada cuánto tiempo el sistema debe solicitar una nueva lectura
 *          completa del sensor de pH. Solo cambiar este valor para ajustar
 *          frecuencia de monitoreo.
 * @note Valor actual: 10 segundos. Ajustar según:
 *       - Aplicaciones de monitoreo continuo: 5-15 segundos
 *       - Sistemas de bajo consumo: 60-300 segundos
 *       - Aplicaciones críticas de reacción rápida: 1-5 segundos
 * @warning No confundir con PH_INTERVAL_MS (definido en .cpp) que controla
 *          la distribución temporal de las 40 muestras individuales.
 */
#define PH_READ_INTERVAL_SECONDS   10

// ——— Estructura de datos del sensor pH  ———

/**
 * @struct pHReading
 * @brief Estructura empaquetada que representa una lectura completa del sensor pH
 * @details Contiene todos los datos relevantes de una medición: valor de pH calculado,
 *          voltaje medido, timestamp, estado de validez y códigos de error.
 *          Empaquetada con __attribute__((packed)) para optimizar memoria en almacenamiento
 *          persistente (RTC Memory, EEPROM, etc.).
 * 
 * @note Tamaño aproximado: 19 bytes (packed)
 * @warning El campo 'temperature' está reservado pero no implementado. Actualmente no
 *          se usa en cálculos de compensación por temperatura.
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;         // Tiempo en milisegundos
    float ph_value;            // Valor de pH (0-14)
    float voltage;             // Voltaje medido del sensor
    float temperature; 
    uint16_t reading_number;   // Número de lectura
    uint8_t sensor_status;     // Estado del sensor (flags)
    bool valid;                // Indica si la lectura es válida
} pHReading;

// ——— Códigos de estado del sensor ———
/**
 * @def PH_STATUS_OK
 * @brief Código de estado: lectura exitosa sin errores
 * @details Todos los bits en 0. Indica que la lectura se completó correctamente,
 *          voltaje y pH están dentro de rangos válidos.
 */
#define PH_STATUS_OK               0x00  // Sin errores

/**
 * @def PH_STATUS_TIMEOUT
 * @brief Flag de timeout: operación de lectura excedió PH_OPERATION_TIMEOUT
 * @details Bit 0. Indica que takeReadingWithTimeout() no pudo completarse en
 *          el tiempo asignado. Posibles causas: ADC bloqueado, hardware desconectado.
 */
#define PH_STATUS_TIMEOUT          0x01  // Flag de timeout

/**
 * @def PH_STATUS_INVALID_READING
 * @brief Flag de lectura inválida: datos inconsistentes o sensor no inicializado
 * @details Bit 1. Indica problemas genéricos en la lectura: sensor no inicializado,
 *          valores NaN, o errores de comunicación con hardware.
 */
#define PH_STATUS_INVALID_READING  0x02  // Flag de lectura inválida

/**
 * @def PH_STATUS_VOLTAGE_LOW
 * @brief Flag de voltaje bajo: voltaje medido < MIN_VALID_VOLTAGE
 * @details Bit 2. Indica que el voltaje está por debajo del rango válido (< 0.1V).
 *          Posibles causas: sensor desconectado, circuito abierto, alimentación insuficiente.
 */
#define PH_STATUS_VOLTAGE_LOW      0x04  // Voltaje muy bajo

/**
 * @def PH_STATUS_VOLTAGE_HIGH
 * @brief Flag de voltaje alto: voltaje medido > MAX_VALID_VOLTAGE
 * @details Bit 3. Indica que el voltaje excede el rango válido (> 3.2V).
 *          Posibles causas: cortocircuito, sensor dañado, amplificador saturado.
 */
#define PH_STATUS_VOLTAGE_HIGH     0x08  // Voltaje muy alto

/**
 * @def PH_STATUS_OUT_OF_RANGE
 * @brief Flag de pH fuera de rango: pH calculado fuera de 0.0 - 14.0
 * @details Bit 4. Indica que el pH resultante está fuera de la escala química válida.
 *          Posibles causas: calibración incorrecta, solución extremadamente ácida/alcalina.
 * @note La escala de pH real es 0-14, valores fuera sugieren error de calibración.
 */
#define PH_STATUS_OUT_OF_RANGE     0x10  // pH fuera de rango (0-14)

/**
 * @namespace pHSensor
 * @brief Espacio de nombres para todas las funcionalidades del sensor de pH
 * @details Encapsula variables, constantes y funciones relacionadas con el sensor pH.
 *          Evita contaminación del namespace global y facilita integración modular.
 */

// ——— Namespace para el sensor de pH ———
namespace pHSensor {
    
    // Constantes de validación

    /**
     * @brief Valor mínimo válido de pH en la escala química
     * @details Límite inferior de la escala de pH. Valores por debajo indican error.
     */
    constexpr float MIN_VALID_PH = 0.0f;

    /**
     * @brief Valor máximo válido de pH en la escala química
     * @details Límite superior de la escala de pH. Valores por encima indican error.
     */
    constexpr float MAX_VALID_PH = 14.0f;

    /**
     * @brief Voltaje mínimo válido del sensor en voltios
     * @details Voltajes por debajo sugieren sensor desconectado o circuito abierto.
     *          Valor típico: 0.1V (margen de seguridad sobre 0V).
     */
    constexpr float MIN_VALID_VOLTAGE = 0.1f;

    /**
     * @brief Voltaje máximo válido del sensor en voltios
     * @details Voltajes por encima sugieren sensor saturado o hardware dañado.
     *          Valor típico: 3.2V (margen de seguridad bajo Vcc=3.3V del ESP32).
     */
    constexpr float MAX_VALID_VOLTAGE = 3.2f;
    
    // ——— Valores de calibración  ———

    /**
     * @brief Offset actual de calibración (intercepto 'b' de pH = m*V + b)
     * @details Variable externa modificable en tiempo de ejecución mediante
     *          setCalibration() o calibrateWithBuffer(). Inicializada con
     *          PH_CALIBRATED_OFFSET del header.
     * @note Unidad: pH (adimensional)
     */
    extern float phOffset;              // Offset de calibración

    /**
     * @brief Pendiente actual de calibración (coeficiente 'm' de pH = m*V + b)
     * @details Variable externa modificable en tiempo de ejecución mediante
     *          setCalibration(). Inicializada con PH_CALIBRATED_SLOPE del header.
     * @note Unidad: pH/V (cambio de pH por voltio)
     */
    extern float phSlope;               // Pendiente de calibración
    
    // ——— Funciones principales ———

    /**
     * @brief Inicializa el sensor de pH en el pin ADC especificado
     * @details Configura ADC con resolución de 12 bits, atenuación de 11dB,
     *          calibración específica del chip ESP32 y limpia buffers de muestras.
     *          Es seguro llamar múltiples veces (verifica estado de inicialización).
     * @param pin Pin GPIO compatible con ADC1 del ESP32 (por defecto PH_SENSOR_PIN)
     * @return true si inicialización exitosa o ya estaba inicializado, false en caso de error
     * @note Requiere llamarse una vez en setup() antes de usar otras funciones.
     * @warning Verificar discrepancia entre ADC_11db y ADC_ATTEN_DB_12 (ver notas en .cpp)
     */
    bool initialize(uint8_t pin = PH_SENSOR_PIN);

    /**
     * @brief Limpia y deshabilita el sensor pH
     * @details Marca el sensor como no inicializado, permitiendo reinicialización.
     *          No libera recursos de hardware, solo resetea estado lógico interno.
     * @note Útil para reset de sistema o cambio de configuración.
     */
    void cleanup();

    /**
     * @brief Realiza una lectura completa de pH (wrapper de takeReadingWithTimeout)
     * @details Función de conveniencia que llama internamente a takeReadingWithTimeout().
     *          El parámetro temperature actualmente no se utiliza en el cálculo.
     * @param temperature Temperatura del agua en °C (RESERVADO para compensación futura)
     * @return Estructura pHReading con resultado completo de la medición
     * @note La compensación por temperatura NO está implementada. Parámetro reservado.
     * @see takeReadingWithTimeout() para detalles completos del proceso de lectura
     */
    pHReading takeReading(float temperature );

    /**
     * @brief Realiza lectura completa de pH con control de timeout y validación exhaustiva
     * @details Proceso completo:
     *          1. Verifica inicialización del sensor
     *          2. Incrementa contador global de lecturas
     *          3. Lee voltaje promediado (40 muestras distribuidas temporalmente)
     *          4. Verifica timeout de operación (< PH_OPERATION_TIMEOUT)
     *          5. Valida rango de voltaje (MIN_VALID_VOLTAGE - MAX_VALID_VOLTAGE)
     *          6. Convierte voltaje a pH usando calibración (pH = slope * V + offset)
     *          7. Valida rango de pH (0.0 - 14.0)
     *          8. Actualiza last_reading y registra errores si corresponde
     * @param temperature Temperatura del agua en °C (actualmente no usado en cálculo)
     * @return Estructura pHReading con campos:
     *         - ph_value: Valor de pH calculado (0.0 si inválido)
     *         - voltage: Voltaje medido en voltios
     *         - timestamp: millis() al momento de la lectura
     *         - reading_number: Número secuencial de lectura
     *         - valid: true si lectura válida y dentro de todos los rangos
     *         - sensor_status: Código bit-field de estado (ver PH_STATUS_*)
     * @note Si hay timeout o valores fuera de rango, decrementa el contador global.
     * @warning Función bloqueante durante PH_INTERVAL_MS (típicamente 10 segundos).
     *          Considerar ejecutar en tarea separada para sistemas de tiempo real.
     */
    pHReading takeReadingWithTimeout(float temperature);
    
    // ——— Funciones de calibración ———

    /**
     * @brief Establece nuevos parámetros de calibración manualmente
     * @details Actualiza phOffset y phSlope con valores proporcionados por el usuario.
     *          Útil después de calibración externa con equipo profesional.
     * @param offset Nuevo valor de offset (intercepto 'b' de pH = m*V + b)
     * @param slope Nueva pendiente (coeficiente 'm' de pH = m*V + b)
     * @note Imprime confirmación de cambios en Serial para verificación.
     */
    void setCalibration(float offset, float slope);

    /**
     * @brief Obtiene los parámetros de calibración actuales por referencia
     * @details Permite al sistema principal consultar calibración sin modificarla.
     *          Útil para guardar configuración en memoria persistente (EEPROM, RTC Memory).
     * @param[out] offset Referencia donde se almacenará el offset actual
     * @param[out] slope Referencia donde se almacenará la pendiente actual
     */
    void getCalibration(float& offset, float& slope);

    /**
     * @brief Restablece la calibración a valores por defecto del header
     * @details Restaura phOffset y phSlope a PH_CALIBRATED_OFFSET y PH_CALIBRATED_SLOPE.
     *          Útil para resetear calibraciones incorrectas o volver a estado de fábrica.
     * @note Imprime confirmación en Serial.
     */
    void resetToDefaultCalibration();

    /**
     * @brief Calibra el sensor usando una solución buffer de pH conocido (un punto)
     * @details Calibración simplificada de un punto: asume pendiente fija y calcula
     *          nuevo offset usando: offset = pH_buffer - slope * voltaje_medido.
     *          Ideal para ajuste rápido con buffer pH 7.0.
     * @param bufferPH Valor de pH conocido de la solución buffer (típico: 4.0, 7.0, 10.0)
     * @param measuredVoltage Voltaje medido con el sensor sumergido en el buffer
     * @return true si calibración realizada exitosamente
     * @note Imprime información detallada del proceso en Serial.
     * @note Para calibración más precisa de dos puntos (ajuste simultáneo de offset + slope),
     *       considerar implementar función adicional que tome dos buffers diferentes.
     * @warning Asegurar que sensor esté estabilizado (≥30 segundos en buffer) antes de calibrar.
     */
    bool calibrateWithBuffer(float bufferPH, float measuredVoltage);
    
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
     * @brief Obtiene el valor de pH de la última lectura almacenada
     * @return Valor de pH (0.0 si última lectura fue inválida)
     */
    float getLastPH();

    /**
     * @brief Obtiene el voltaje de la última lectura almacenada
     * @return Voltaje en voltios
     */
    float getLastVoltage();

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
     * @details Muestra: número de lectura, pH, voltaje, timestamp, estado de validez.
     *          Útil para depuración y monitoreo en tiempo real.
     * @note Si no hay lecturas previas (reading_number == 0), informa al usuario.
     */
    void printLastReading();

    /**
     * @brief Valida si un valor de pH está dentro del rango químico aceptable
     * @param ph Valor de pH a validar
     * @return true si pH está entre MIN_VALID_PH (0.0) y MAX_VALID_PH (14.0) y no es NaN
     */
    bool isPHInRange(float ph);

    /**
     * @brief Valida si un voltaje está dentro del rango aceptable del sensor
     * @param voltage Voltaje en voltios a validar
     * @return true si voltaje está entre MIN_VALID_VOLTAGE (0.1V) y MAX_VALID_VOLTAGE (3.2V) y no es NaN
     */
    bool isVoltageInRange(float voltage);

    /**
     * @brief Clasifica el tipo de agua según su pH
     * @param ph Valor de pH a clasificar
     * @return String descriptivo del tipo de agua:
     *         - "Muy ácida" (pH < 6.0)
     *         - "Ácida" (6.0 ≤ pH < 6.5)
     *         - "Ligeramente ácida" (6.5 ≤ pH < 7.0)
     *         - "Neutra" (pH == 7.0)
     *         - "Ligeramente alcalina" (7.0 < pH < 7.5)
     *         - "Alcalina" (7.5 ≤ pH < 8.5)
     *         - "Muy alcalina" (pH ≥ 8.5)
     * @note Útil para interpretación rápida de resultados y logs legibles.
     */
    String getWaterType(float ph);


    
    // ——— Funciones para integración con sistema principal ———

    /**
     * @brief Vincula el sensor con un contador global de lecturas del sistema
     * @details Permite que el módulo pH incremente automáticamente un contador externo
     *          en cada lectura válida. Útil para estadísticas globales del sistema.
     * @param total_readings_ptr Puntero a uint16_t que será incrementado en cada lectura válida
     * @note El puntero debe apuntar a memoria válida durante toda la vida útil del sensor.
     * @warning No pasar punteros a variables locales que puedan salir de scope.
     */
    void setReadingCounter(uint16_t* total_readings_ptr);

    /**
     * @brief Vincula el sensor con un sistema de logging de errores externo
     * @details Permite que el módulo pH reporte errores (timeout, lectura inválida, etc.)
     *          a un sistema centralizado de gestión de errores o logger.
     * @param log_error_func Puntero a función con firma: void(int code, int severity, uint32_t context)
     *        - code: Código de error (1=timeout, 2=lectura inválida, etc.)
     *        - severity: Nivel de severidad (1=warning, 2=error, 3=crítico, etc.)
     *        - context: Información contextual (tiempo transcurrido, voltaje*1000, pH*100, etc.)
     * @note La función debe ser thread-safe si se usa en entorno multitarea (FreeRTOS).
     * @warning No pasar punteros a funciones lambda sin captura estática.
     */
    void setErrorLogger(void (*log_error_func)(int, int, uint32_t));
    
    // ——— Variables internas del módulo (extern para acceso desde .cpp)———

    /**
     * @brief Bandera de estado de inicialización del sensor
     * @details Indica si initialize() fue llamado exitosamente. Evita operaciones sobre
     *          hardware no configurado.
     */
    extern bool initialized;

    /**
     * @brief Pin GPIO asignado al ADC para lectura del sensor pH
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
     *          Accesible mediante getLastPH(), getLastVoltage(), etc.
     */
    extern pHReading last_reading;

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
    
    // ——— Buffer de muestras para promediado ———

    /**
     * @brief Array circular de muestras crudas del ADC
     * @details Almacena PH_ARRAY_LENGTH (40) lecturas consecutivas del ADC para
     *          promediado estadístico con descarte de extremos. Reduce ruido y outliers.
     */
    extern int phArray[PH_ARRAY_LENGTH];

    /**
     * @brief Índice actual en el array circular de muestras
     * @details Apunta a la próxima posición disponible en phArray[] para escribir.
     *          Reiniciado a 0 en initialize().
     */
    extern int phArrayIndex;
    
    // ——— Funciones adicionales para debugging ———

    /**
     * @brief Muestra información completa de calibración y estado del sensor por Serial
     * @details Imprime:
     *          - Estado de inicialización (inicializado / no inicializado)
     *          - Pin ADC configurado
     *          - Ecuación de calibración actual (pH = slope * V + offset)
     *          - Rangos válidos de pH (0.0 - 14.0) y voltaje (0.1V - 3.2V)
     *          - Información de última lectura válida si existe
     * @note Útil para verificación rápida de configuración y diagnóstico de problemas.
     */
    void showCalibrationInfo();

    /**
     * @brief Realiza una lectura de prueba y muestra resultados detallados
     * @details Lee voltaje promediado, valida rango, calcula pH y muestra cada paso
     *          del proceso en Serial. No actualiza last_reading ni contadores globales.
     *          Ideal para verificación rápida sin afectar estadísticas del sistema.
     * @note Requiere sensor inicializado. Función bloqueante durante PH_INTERVAL_MS.
     */
    void testReading();

    /**
     * @brief Ejecuta rutina interactiva de calibración con buffer pH 7.0
     * @details Guía paso a paso al usuario para calibrar el sensor:
     *          1. Sumerge sensor en buffer pH 7.0
     *          2. Espera estabilización (30 segundos recomendados)
     *          3. Espera input del usuario en Serial
     *          4. Lee voltaje promediado
     *          5. Calcula y aplica nuevo offset (asumiendo pendiente fija)
     * @note Calibración de un punto. Para calibración de dos puntos (offset + slope),
     *       considerar implementar función extendida con dos buffers diferentes.
     * @note Requiere interacción por Serial Monitor. Función bloqueante hasta recibir input.
     * @warning Asegurar que sensor esté correctamente sumergido y estabilizado antes de
     *          enviar cualquier carácter por Serial para continuar el proceso.
     */
    void performCalibrationRoutine();
}

#endif // PH_SENSOR_H