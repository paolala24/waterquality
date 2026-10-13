/**
 * @file Turbidez.h
 * @brief Definición del módulo de sensor de turbidez para ESP32
 * @details Este header contiene todas las definiciones, estructuras, constantes y
 *          prototipos de funciones para el manejo del sensor analógico de turbidez
 *          conectado al ADC del ESP32. Implementa conversión voltaje→NTU mediante
 *          algoritmo segmentado calibrado experimentalmente.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#ifndef TURBIDITY_SENSOR_H
#define TURBIDITY_SENSOR_H

#include <Arduino.h>
#include <esp_adc_cal.h>

// ——— Configuración del sensor de turbidez ———

/**
 * @def TURBIDITY_SENSOR_PIN
 * @brief Pin GPIO del ADC para conexión del sensor de turbidez
 * @details Debe ser un pin compatible con ADC1. El proyecto usa una placa
 *          ESP32-S2 Kaluga, donde GPIO5 es un pin ADC1 válido.
 * @note No usar pines de ADC2 si WiFi está activo (conflicto de hardware).
 */
#define TURBIDITY_SENSOR_PIN    5

/**
 * @def TURBIDITY_OPERATION_TIMEOUT
 * @brief Timeout máximo para operación completa de lectura en milisegundos
 * @details Si la función takeReadingWithTimeout() excede este tiempo, retorna
 *          error de timeout (TURBIDITY_STATUS_TIMEOUT) y registra el evento.
 * @note El timeout se comprueba durante el muestreo; una lectura normal toma
 *       aproximadamente 50 ms.
 */
#define TURBIDITY_OPERATION_TIMEOUT  5000  // Timeout para operación del sensor

// ——— Valores de calibración obtenidos experimentalmente ———

/**
 * @def TURBIDITY_MAX_VOLTAGE
 * @brief Voltaje máximo calibrado del sensor en agua muy clara
 * @details Voltaje medido experimentalmente en condiciones de agua cristalina (NTU≈0).
 *          Usado como punto de referencia superior en algoritmo de conversión.
 * @note Unidad: Voltios (V). Valor experimental específico del sensor usado.
 */
#define TURBIDITY_MAX_VOLTAGE   2.179100f  

/**
 * @def TURBIDITY_MIN_VOLTAGE
 * @brief Voltaje mínimo calibrado del sensor en agua muy turbia
 * @details Voltaje medido experimentalmente en condiciones de agua extremadamente turbia.
 *          Usado como punto de referencia inferior en algoritmo de conversión.
 * @note Unidad: Voltios (V). Valor experimental específico del sensor usado.
 */
#define TURBIDITY_MIN_VOLTAGE   0.653200f  

// ——— Estructura de datos del sensor de turbidez ———

/**
 * @struct TurbidityReading
 * @brief Estructura empaquetada que representa una lectura completa del sensor de turbidez
 * @details Contiene todos los datos relevantes de una medición: turbidez en NTU, voltaje
 *          medido, timestamp, estado de validez y códigos de error. Empaquetada con
 *          __attribute__((packed)) para optimizar memoria en almacenamiento persistente
 *          (RTC Memory, EEPROM, etc.).
 * 
 * @note Tamaño aproximado: 16 bytes (packed)
 * @note NTU (Nephelometric Turbidity Units) es la unidad estándar internacional para
 *       medir turbidez. 1 NTU = dispersión de luz en solución de formazina calibrada.
 */
typedef struct __attribute__((packed)) {
    uint32_t timestamp;         ///< Timestamp en milisegundos (millis()) del momento de la lectura
    float turbidity_ntu;        ///< Valor de turbidez en NTU (Nephelometric Turbidity Units)
    float voltage;              ///< Voltaje medido del sensor en voltios (0.0 - 3.3V típico)
    uint16_t reading_number;    ///< Número secuencial de lectura desde inicio del sistema
    uint8_t sensor_status;      ///< Código de estado bit-field (ver TURBIDITY_STATUS_*)
    bool valid;                 ///< Bandera de validez: true si lectura exitosa y dentro de rangos
} TurbidityReading;

// ——— Códigos de estado del sensor ———

/**
 * @def TURBIDITY_STATUS_OK
 * @brief Código de estado: lectura exitosa sin errores
 * @details Todos los bits en 0. Indica que la lectura se completó correctamente,
 *          voltaje y turbidez están dentro de rangos válidos.
 */
#define TURBIDITY_STATUS_OK              0x00  // Sin errores

/**
 * @def TURBIDITY_STATUS_TIMEOUT
 * @brief Flag de timeout: operación de lectura excedió TURBIDITY_OPERATION_TIMEOUT
 * @details Bit 0. Indica que takeReadingWithTimeout() no pudo completarse en el
 *          tiempo asignado. Posibles causas: ADC bloqueado, hardware desconectado.
 */
#define TURBIDITY_STATUS_TIMEOUT         0x01  // Flag de timeout

/**
 * @def TURBIDITY_STATUS_INVALID_READING
 * @brief Flag de lectura inválida: datos inconsistentes o sensor no inicializado
 * @details Bit 1. Indica problemas genéricos en la lectura: sensor no inicializado,
 *          valores NaN, o errores de comunicación con hardware.
 */
#define TURBIDITY_STATUS_INVALID_READING 0x02  // Flag de lectura inválida

/**
 * @def TURBIDITY_STATUS_VOLTAGE_LOW
 * @brief Flag de voltaje bajo: voltaje medido < MIN_VALID_VOLTAGE
 * @details Bit 2. Indica que el voltaje está por debajo del rango válido (< 0.1V).
 *          Posibles causas: sensor desconectado, circuito abierto, alimentación insuficiente.
 */
#define TURBIDITY_STATUS_VOLTAGE_LOW     0x04  // Voltaje muy bajo

/**
 * @def TURBIDITY_STATUS_VOLTAGE_HIGH
 * @brief Flag de voltaje alto: voltaje medido > MAX_VALID_VOLTAGE
 * @details Bit 3. Indica que el voltaje excede el rango válido (> 2.5V).
 *          Posibles causas: cortocircuito, sensor dañado, amplificador saturado.
 */
#define TURBIDITY_STATUS_VOLTAGE_HIGH    0x08  // Voltaje muy alto

/**
 * @def TURBIDITY_STATUS_OVERFLOW
 * @brief Flag de overflow: turbidez calculada excede MAX_VALID_NTU
 * @details Bit 4. Indica que la turbidez resultante está por encima del rango máximo
 *          medible (> 3000 NTU). Posible en aguas extremadamente contaminadas.
 * @note Diferente a VOLTAGE_HIGH: voltaje puede estar en rango válido pero el cálculo
 *       NTU resulta en un valor que excede capacidad del sensor o calibración.
 */
#define TURBIDITY_STATUS_OVERFLOW        0x10  // Turbidez fuera de rango

// ——— Namespace para el sensor de turbidez ———

/**
 * @namespace TurbiditySensor
 * @brief Espacio de nombres para todas las funcionalidades del sensor de turbidez
 * @details Encapsula variables, constantes y funciones relacionadas con el sensor de turbidez.
 *          Evita contaminación del namespace global y facilita integración modular.
 */
namespace TurbiditySensor {
    
    // Constantes de validación

    /**
     * @brief Valor mínimo válido de turbidez en NTU
     * @details Límite inferior del rango de medición. Turbidez negativa indica error
     *          de calibración (físicamente imposible).
     */
    constexpr float MIN_VALID_NTU = 0.0;

    /**
     * @brief Valor máximo válido de turbidez en NTU
     * @details Límite superior del rango de medición del sensor. Valores por encima
     *          de 3000 NTU exceden capacidad del sensor o indican agua extremadamente
     *          contaminada fuera del rango de calibración.
     * @note Agua potable según OMS debe tener turbidez < 5 NTU.
     */
    constexpr float MAX_VALID_NTU = 3000.0;

    /**
     * @brief Voltaje mínimo válido del sensor en voltios
     * @details Voltajes por debajo (< 0.1V) sugieren sensor desconectado o circuito abierto.
     *          Margen de seguridad sobre 0V para detectar lecturas espurias.
     */
    constexpr float MIN_VALID_VOLTAGE = 0.1;   // Voltaje mínimo válido

    /**
     * @brief Voltaje máximo válido del sensor en voltios
     * @details Voltajes por encima (> 2.5V) sugieren sensor saturado o hardware dañado.
     *          Sensores de turbidez típicamente operan entre 0V (turbia) y 2.2V (clara).
     * @note Límite 2.5V proporciona margen de seguridad sobre voltaje máximo esperado.
     */
    constexpr float MAX_VALID_VOLTAGE = 2.5;   // Voltaje máximo válido
    
    // Coeficientes de calibración
    // Ecuación: NTU = a*V³ + b*V² + c*V + d

    /**
     * @brief Coeficiente cúbico (A) de la ecuación polinómica de calibración
     * @details Término de tercer grado en: NTU = A×V³ + B×V² + C×V + D
     *          Calibrado experimentalmente para el sensor específico usado.
     * @warning NOTA: Estos coeficientes actualmente NO se usan en voltageToNTU() que
     *          implementa algoritmo segmentado. Se conservan para compatibilidad futura
     *          si se decide implementar calibración polinómica pura.
     */
    constexpr float CALIB_COEFF_A = -1120.4f;  // Coeficiente cúbico

    /**
     * @brief Coeficiente cuadrático (B) de la ecuación polinómica de calibración
     * @details Término de segundo grado en: NTU = A×V³ + B×V² + C×V + D
     *          Calibrado experimentalmente para el sensor específico usado.
     * @warning NOTA: Ver advertencia en CALIB_COEFF_A.
     */
    constexpr float CALIB_COEFF_B = 5742.3f;   // Coeficiente cuadrático 
    
    /**
     * @brief Coeficiente lineal (C) de la ecuación polinómica de calibración
     * @details Término de primer grado en: NTU = A×V³ + B×V² + C×V + D
     *          Calibrado experimentalmente para el sensor específico usado.
     * @warning NOTA: Ver advertencia en CALIB_COEFF_A.
     */
    constexpr float CALIB_COEFF_C = -4352.9f;  // Coeficiente lineal

    /**
     * @brief Término independiente (D) de la ecuación polinómica de calibración
     * @details Término constante en: NTU = A×V³ + B×V² + C×V + D
     *          Calibrado experimentalmente para el sensor específico usado.
     * @warning NOTA: Ver advertencia en CALIB_COEFF_A.
     */
    constexpr float CALIB_COEFF_D = -2500.0f;  // Término independiente
    
    // ——— Funciones principales  ———

    /**
     * @brief Inicializa el sensor de turbidez en el pin ADC especificado
     * @details Configura el ADC a 13 bits, con atenuación de 11 dB y calibración
     *          compatible con el ESP32-S2.
     *          Es seguro llamar múltiples veces (verifica estado de inicialización).
     * @param pin Pin GPIO compatible con ADC1 del ESP32 (por defecto TURBIDITY_SENSOR_PIN)
     * @return true si inicialización exitosa o ya estaba inicializado
     * @note Requiere llamarse una vez en setup() antes de usar otras funciones.
     */
    bool initialize(uint8_t pin = TURBIDITY_SENSOR_PIN);

    /**
     * @brief Limpia y deshabilita el sensor de turbidez
     * @details Marca el sensor como no inicializado, permitiendo reinicialización.
     *          No libera recursos de hardware, solo resetea estado lógico interno.
     * @note Útil para reset de sistema o cambio de configuración.
     */
    void cleanup();

    /**
     * @brief Realiza una lectura completa de turbidez (wrapper de takeReadingWithTimeout)
     * @details Función de conveniencia que llama internamente a takeReadingWithTimeout().
     * @return Estructura TurbidityReading con resultado completo de la medición
     * @see takeReadingWithTimeout() para detalles completos del proceso de lectura
     */
    TurbidityReading takeReading();

    /**
     * @brief Realiza lectura completa de turbidez con control de timeout y validación exhaustiva
     * @details Proceso completo:
     *          1. Verifica inicialización del sensor
     *          2. Incrementa contador global de lecturas
     *          3. Lee voltaje calibrado (50 muestras promediadas)
     *          4. Verifica timeout de operación (< TURBIDITY_OPERATION_TIMEOUT)
     *          5. Valida rango de voltaje (0.1V - 2.5V)
     *          6. Convierte voltaje a NTU usando voltageToNTU() con algoritmo segmentado
     *          7. Valida rango de turbidez (0 - 3000 NTU)
     *          8. Actualiza last_reading y registra errores si corresponde
     * @return Estructura TurbidityReading con campos:
     *         - turbidity_ntu: Turbidez en NTU (0.0 si inválida)
     *         - voltage: Voltaje medido en voltios
     *         - timestamp: millis() al momento de la lectura
     *         - reading_number: Número secuencial de lectura
     *         - valid: true si lectura válida y dentro de todos los rangos
     *         - sensor_status: Código bit-field de estado (ver TURBIDITY_STATUS_*)
     * @note Si hay timeout o valores fuera de rango, decrementa el contador global.
     * @warning Función bloqueante por aproximadamente 50 ms (tiempo de muestreo
     *          del ADC); cede tiempo al sistema entre muestras.
     */
    TurbidityReading takeReadingWithTimeout();
    
    // ——— Funciones de calibración ———

    /**
     * @brief Convierte voltaje medido a turbidez en NTU usando algoritmo segmentado
     * @details Implementa conversión por segmentos de voltaje para mejor precisión:
     *          - V > 2.15V: Rango de agua muy clara (0-10 NTU)
     *          - V < 0.7V: Rango de agua muy turbia (>1000 NTU)
     *          - 0.7V ≤ V ≤ 2.15V: Rango medio (10-1500 NTU)
     *          Cada segmento usa interpolación lineal ajustada experimentalmente.
     * @param voltage Voltaje medido del sensor en voltios
     * @return Turbidez en NTU (Nephelometric Turbidity Units). Mínimo 0 NTU.
     * @note El algoritmo segmentado mejora linealidad en rangos extremos donde
     *       el polinomio cúbico pierde precisión.
     */
    float voltageToNTU(float voltage);

    /**
     * @brief Alias de voltageToNTU() para compatibilidad con API antigua
     * @param rawVoltage Voltaje crudo del sensor en voltios
     * @return Turbidez calibrada en NTU
     * @see voltageToNTU() para detalles del algoritmo
     */
    float calibrateReading(float rawVoltage);

    /**
     * @brief Establece nuevos coeficientes del polinomio de calibración
     * @details Actualiza coeficientes de la ecuación: NTU = a×V³ + b×V² + c×V + d
     * @param a Coeficiente cúbico
     * @param b Coeficiente cuadrático
     * @param c Coeficiente lineal
     * @param d Término independiente
     * @note Imprime confirmación de cambios en Serial.
     * @warning Los coeficientes actualizados NO se usan en voltageToNTU() que implementa
     *          algoritmo segmentado. Conservados para compatibilidad futura.
     */
    void setCalibrationCoefficients(float a, float b, float c, float d);

    /**
     * @brief Obtiene los coeficientes de calibración actuales por referencia
     * @param[out] a Referencia donde se almacenará el coeficiente cúbico
     * @param[out] b Referencia donde se almacenará el coeficiente cuadrático
     * @param[out] c Referencia donde se almacenará el coeficiente lineal
     * @param[out] d Referencia donde se almacenará el término independiente
     */
    void getCalibrationCoefficients(float& a, float& b, float& c, float& d);
    
    /**
     * @brief Restablece los coeficientes de calibración a valores por defecto del header
     * @details Restaura calib_a/b/c/d a CALIB_COEFF_A/B/C/D.
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
     * @brief Obtiene el valor de turbidez de la última lectura almacenada
     * @return Turbidez en NTU (0.0 si última lectura fue inválida)
     */
    float getLastTurbidity();

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
     * @details Muestra: número de lectura, turbidez NTU, voltaje, timestamp, estado,
     *          calidad del agua y categoría de turbidez.
     * @note Si no hay lecturas previas (reading_number == 0), informa al usuario.
     */
    void printLastReading();

    /**
     * @brief Valida si un valor de turbidez está dentro del rango aceptable
     * @param ntu Valor de turbidez a validar en NTU
     * @return true si ntu está entre MIN_VALID_NTU (0.0) y MAX_VALID_NTU (3000.0) y no es NaN
     */
    bool isTurbidityInRange(float ntu);

    /**
     * @brief Valida si un voltaje está dentro del rango aceptable del sensor
     * @param voltage Voltaje en voltios a validar
     * @return true si voltaje está entre MIN_VALID_VOLTAGE (0.1V) y MAX_VALID_VOLTAGE (2.5V) y no es NaN
     */
    bool isVoltageInRange(float voltage);

    /**
     * @brief Clasifica la calidad del agua según su turbidez
     * @param ntu Valor de turbidez a clasificar en NTU
     * @return String descriptivo de la calidad del agua:
     *         - "Excelente" (NTU ≤ 1) - Agua cristalina
     *         - "Muy buena" (1 < NTU ≤ 4) - Agua muy clara
     *         - "Buena" (4 < NTU ≤ 10) - Agua clara
     *         - "Aceptable" (10 < NTU ≤ 25) - Agua ligeramente turbia
     *         - "Pobre" (25 < NTU ≤ 100) - Agua turbia
     *         - "Muy pobre" (NTU > 100) - Agua muy turbia/no potable
     * @note Clasificación según estándares EPA y OMS para agua potable (límite 5 NTU).
     */
    String getWaterQuality(float ntu);

    /**
     * @brief Clasifica la categoría visual de turbidez del agua
     * @param ntu Valor de turbidez a clasificar en NTU
     * @return String descriptivo de la apariencia visual:
     *         - "Agua muy clara" (NTU ≤ 1)
     *         - "Agua clara" (1 < NTU ≤ 4)
     *         - "Ligeramente turbia" (4 < NTU ≤ 10)
     *         - "Moderadamente turbia" (10 < NTU ≤ 25)
     *         - "Turbia" (25 < NTU ≤ 100)
     *         - "Muy turbia" (100 < NTU ≤ 400)
     *         - "Extremadamente turbia" (NTU > 400)
     * @note Útil para interpretación rápida de resultados y logs legibles.
     */
    String getTurbidityCategory(float ntu);
    
    // ——— Funciones para integración con sistema principal ———

    /**
     * @brief Vincula el sensor con un contador global de lecturas del sistema
     * @details Permite que el módulo de turbidez incremente automáticamente un contador
     *          externo en cada lectura válida. Útil para estadísticas globales del sistema.
     * @param total_readings_ptr Puntero a uint16_t que será incrementado en cada lectura válida
     * @note El puntero debe apuntar a memoria válida durante toda la vida útil del sensor.
     * @warning No pasar punteros a variables locales que puedan salir de scope.
     */
    void setReadingCounter(uint16_t* total_readings_ptr);

    /**
     * @brief Vincula el sensor con un sistema de logging de errores externo
     * @details Permite que el módulo de turbidez reporte errores (timeout, lectura inválida,
     *          etc.) a un sistema centralizado de gestión de errores o logger.
     * @param log_error_func Puntero a función con firma: void(int code, int severity, uint32_t context)
     *        - code: Código de error (1=timeout, 2=lectura inválida, etc.)
     *        - severity: Nivel de severidad (1=warning, 2=error, 3=crítico, etc.)
     *        - context: Información contextual (tiempo transcurrido, voltaje*1000, NTU, etc.)
     * @note La función debe ser thread-safe si se usa en entorno multitarea (FreeRTOS).
     * @warning No pasar punteros a funciones lambda sin captura estática.
     */
    void setErrorLogger(void (*log_error_func)(int, int, uint32_t));
    
    // ——— Variables internas del módulo ———

    /**
     * @brief Bandera de estado de inicialización del sensor de turbidez
     * @details Indica si initialize() fue llamado exitosamente. Evita operaciones sobre
     *          hardware no configurado.
     */
    extern bool initialized;

    /**
     * @brief Pin GPIO asignado al ADC para lectura del sensor de turbidez
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
     *          Accesible mediante getLastTurbidity(), getLastVoltage(), etc.
     */
    extern TurbidityReading last_reading;

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
     *          50 muestras (vs 30 en TDS) proporciona mayor estabilidad para sensores
     *          de turbidez que tienden a tener más variabilidad por partículas en suspensión.
     * @note Cada muestra tiene 1ms de delay, por lo que 50 muestras = ~50ms bloqueado.
     * @note Mayor cantidad de muestras → mayor estabilidad pero mayor tiempo de lectura.
     *       Balancear según requisitos de tiempo real del sistema.
     */
    constexpr int SAMPLES = 50;  // Número de lecturas a promediar
    
    // ——— Funciones adicionales para debugging ———

    /**
     * @brief Muestra información completa de calibración y estado del sensor por Serial
     * @details Imprime:
     *          - Estado de inicialización (inicializado / no inicializado)
     *          - Pin ADC configurado
     *          - Ecuación de calibración polinómica cúbica
     *          - Rango válido de turbidez (0 - 3000 NTU)
     *          - Rango válido de voltaje (0.1V - 2.5V)
     *          - Información de última lectura válida si existe
     * @note Útil para verificación rápida de configuración y diagnóstico de problemas.
     */
    void showCalibrationInfo();

    /**
     * @brief Realiza una lectura de prueba y muestra resultados detallados paso a paso
     * @details Lee voltaje calibrado, calcula turbidez usando voltageToNTU(), y muestra
     *          cada etapa del proceso. No actualiza last_reading ni contadores globales.
     *          Ideal para verificación rápida sin afectar estadísticas del sistema.
     * @note Requiere sensor inicializado. Función bloqueante por ~50ms.
     */
    void testReading();

    /**
     * @brief Función de debug para verificar voltaje crudo y conversión a NTU
     * @details Toma muestras del ADC, muestra:
     *          - Valor ADC promedio crudo
     *          - Voltaje calculado con calibración ESP32
     *          - Turbidez estimada usando voltageToNTU()
     * @note Útil para diagnosticar problemas de calibración o ADC.
     * @note No actualiza last_reading ni contadores. Solo para depuración.
     * @warning Requiere sensor inicializado. Función bloqueante por ~50ms.
     */
    void debugVoltageReading();

    /**
     * @brief Imprime curva completa de calibración voltaje vs NTU
     * @details Muestra tabla con conversión V→NTU en incrementos de 0.1V desde 0.6V hasta 2.2V.
     *          Útil para verificar comportamiento del algoritmo de conversión en todo el rango.
     * @note Permite visualizar linealidad y detectar anomalías en la calibración.
     * @note Usa el algoritmo segmentado actual implementado en voltageToNTU().
     */
    void printCalibrationCurve();
}

#endif // TURBIDITY_SENSOR_H