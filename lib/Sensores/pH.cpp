/**
 * @file pH.cpp
 * @brief Implementación del sensor de pH para ESP32
 * @details Este archivo contiene la lógica para inicialización, lectura,
 *          calibración, validación y pruebas del sensor de pH en un sistema
 *          basado en ESP32. Se utilizan promedios de muestras y se aplican
 *          rutinas de calibración para obtener mediciones confiables.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */
#include "pH.h"

/**
 * @namespace pHSensor
 * @brief Espacio de nombres para todas las funcionalidades del sensor pH
 * 
 * Contiene variables globales internas, funciones de lectura, calibración,
 * validación y utilidades para manejo completo del sensor analógico de pH
 * conectado al ADC del ESP32.
 */

// ——— Variables internas del módulo ———
namespace pHSensor {
    
    // ——— Variables internas del módulo ———
    
    /**
     * @brief Bandera de estado de inicialización del sensor
     * @details Indica si el sensor ha sido correctamente inicializado y está
     *          listo para realizar lecturas. Evita operaciones sobre hardware
     *          no configurado.
     */
    bool initialized = false;

    /**
     * @brief Pin GPIO asignado al ADC para lectura del sensor pH
     * @details Debe ser un pin compatible con ADC1 del ESP32. Por defecto
     *          toma el valor de PH_SENSOR_PIN definido en pH.h
     */

    uint8_t sensor_pin = PH_SENSOR_PIN;

    /**
     * @brief Timestamp de la última lectura válida realizada
     * @details Almacena millis() del momento en que se completó exitosamente
     *          una lectura. Útil para calcular intervalos entre mediciones.
     */

    uint32_t last_reading_time = 0;

    /**
     * @brief Última estructura de lectura capturada por el sensor
     * @details Contiene pH, voltaje, timestamp, estado y número de lectura.
     *          Se actualiza en cada llamada a takeReadingWithTimeout().
     */
    pHReading last_reading = {0};

    /**
     * @brief Características de calibración del ADC del ESP32
     * @details Estructura que almacena los parámetros de calibración específicos
     *          del chip para convertir valores crudos ADC a voltajes reales (mV).
     */

    esp_adc_cal_characteristics_t adc_chars;
    
    // Variables de calibración

    /**
     * @brief Offset de calibración del sensor pH (intercepto)
     * @details Valor 'b' en la ecuación lineal pH = m*V + b.
     *          Inicializado con PH_CALIBRATED_OFFSET del header.
     * @warning Verificar valores de calibración según sensor específico usado.
     */

    float phOffset = PH_CALIBRATED_OFFSET;      //Posible problema ? verificar valor de las variables para la calibración
    /**
     * @brief Pendiente de calibración del sensor pH (slope)
     * @details Valor 'm' en la ecuación lineal pH = m*V + b.
     *          Inicializado con PH_CALIBRATED_SLOPE del header.
     * @warning Verificar valores de calibración según sensor específico usado.
     */
    float phSlope = PH_CALIBRATED_SLOPE;
    
    // Buffer de muestras para promediado
    /**
     * @brief Array circular de muestras crudas del ADC
     * @details Almacena lecturas consecutivas del ADC para posteriormente
     *          promediarlas y descartar valores extremos (filtrado estadístico).
     */
    int phArray[PH_ARRAY_LENGTH];

    /**
     * @brief Índice actual en el array circular de muestras
     * @details Apunta a la próxima posición disponible en phArray[] para
     *          escribir una nueva muestra. Se reinicia en inicialización.
     */

    int phArrayIndex = 0;
    
    // Configuración ADC
    /**
     * @brief Resolución en bits del ADC configurado
     * @details ESP32 soporta 12 bits de resolución (0-4095).
     *          Usado para configurar analogReadResolution().
     */
    const int ADC_BITS = 12;

    /**
     * @brief Valor máximo del ADC según resolución de 12 bits
     * @details 2^12 - 1 = 4095. Representa el valor digital máximo
     *          que puede retornar analogRead() con 12 bits.
     */
    const int ADC_MAX_VALUE = 4095;

    /**
     * @brief Voltaje de referencia interno del ADC en mV
     * @details Valor típico 1100 mV para ESP32. Usado en calibración
     *          con esp_adc_cal_characterize() para ajustar lecturas.
     */
    const int ADC_VREF = 1100;  // mV
    
    // Variables de integración con sistema principal

    /**
     * @brief Puntero al contador global de lecturas del sistema
     * @details Permite incrementar un contador externo cada vez que se realiza
     *          una lectura válida. nullptr si no se ha vinculado con sistema.
     */
    uint16_t* total_readings_counter = nullptr;

    /**
     * @brief Puntero a función de logging de errores del sistema
     * @details Callback para reportar errores (timeout, lectura inválida, etc.)
     *          al sistema principal. nullptr si no está configurado.
     */
    void (*error_logger)(int code, int severity, uint32_t context) = nullptr;
    
    // >>> CAMBIO: Intervalo de muestreo configurable (ms)
    // >>> Para pruebas ahora lo dejamos en 20000 ms = 20 s.
    // >>> Si en el futuro quieres otro intervalo, cambia solo este valor.
    
    /**
     * @brief Intervalo de muestreo configurable en milisegundos
     * @details Define el tiempo total durante el cual se distribuyen las muestras
     *          múltiples del array phArray[]. Actualmente 10000 ms = 10 segundos.
     * @note LÍNEA CRÍTICA PARA MODIFICAR INTERVALO DE MUESTREO.
     *       Cambiar este valor según necesidades del proyecto (ej: 20000 para 20s).
     */
     const unsigned long PH_INTERVAL_MS = 10000UL; // >>> ESTA ES LA LÍNEA QUE DEBES MODIFICAR (ms)
    // Espacio mínimo entre muestras individuales (ms) para evitar muestreo demasiado rápido
    
    /**
     * @brief Espaciado mínimo entre muestras individuales en milisegundos
     * @details Evita muestreo excesivamente rápido del ADC que podría introducir
     *          ruido o sobrecargar el procesador. Valor típico 20 ms.
     * @note Opcional ajustar si se requiere mayor/menor frecuencia de muestreo.
     */
    const unsigned long PH_MIN_SAMPLE_SPACING_MS = 20UL; // >>> Opcional ajustar si quieres cambiar spacing   

    // ——— FUNCIONES INTERNAS ———

    /**
     * @brief Calcula el promedio de un arreglo de muestras, descartando extremos
     * @details Implementa filtrado estadístico: si hay ≥5 muestras, descarta el
     *          valor máximo y mínimo antes de promediar. Reduce efecto de outliers.
     * @param arr Arreglo de enteros con valores crudos del ADC
     * @param number Número de muestras válidas en el arreglo
     * @return Promedio calculado como valor double. Retorna 0 si number ≤ 0.
     * @warning Si hay múltiples valores iguales al máximo o mínimo, todos se descartan.
     *          Esto podría eliminar datos válidos si el valor real está en un extremo.
     * @note Considerar mejorar algoritmo para descartar solo UN máximo y UN mínimo.
     */

    double averageArray(int* arr, int number) {
        if (number <= 0) return 0;
        
        long sum = 0;
        
        if (number < 5) {
            // Si hay pocas muestras, promediar todas
            for (int i = 0; i < number; i++) {
                sum += arr[i];
            }
            return (double)sum / number;
        } else {
            // Descartar máximo y mínimo
            int minv = arr[0];
            int maxv = arr[0];
            
            // Encontrar máximo y mínimo
            for (int i = 1; i < number; i++) {
                if (arr[i] < minv) minv = arr[i];
                if (arr[i] > maxv) maxv = arr[i];
            }
            
            // Sumar todos excepto máximo y mínimo
            //si hay varios minimos o maximos descarta todos los iguales podría ser malo si el valor real está en uno de los extremos
            int count = 0;
            for (int i = 0; i < number; i++) {
                if (arr[i] != minv && arr[i] != maxv) {
                    sum += arr[i];
                    count++;
                }
            }
            
            return count > 0 ? (double)sum / count : 0;
        }
    }
    
    /**
     * @brief Lee y promedia múltiples muestras del sensor pH con distribución temporal
     * @details Toma PH_ARRAY_LENGTH muestras distribuidas durante PH_INTERVAL_MS,
     *          respetando un spacing mínimo entre lecturas. No usa delay() bloqueante.
     *          Convierte el promedio crudo del ADC a voltaje usando calibración ESP32.
     * @return Voltaje promedio en voltios (float). Rango típico 0.0 - 3.3V.
     * @note Utiliza esp_adc_cal_raw_to_voltage() para conversión calibrada.
     * @warning Existe discrepancia entre analogReadResolution(12) y ADC_WIDTH_BIT_13
     *          usado en esp_adc_cal_characterize(). Se mantiene 12 bits por consistencia.
     *          PENDIENTE: Verificar y unificar configuración ADC.
     */

    float readAveragedVoltage() {
        // Tomar múltiples muestras con el intervalo configurado
        unsigned long startTime = millis();
        int sampleCount = 0;

    // Distribuir las muestras durante PH_INTERVAL_MS evitando delay() bloqueante
    // Calcular intervalo por muestra; respetar spacing mínimo para evitar lecturas muy rápidas
    unsigned long perSampleInterval = PH_INTERVAL_MS / (PH_ARRAY_LENGTH > 0 ? PH_ARRAY_LENGTH : 1);
    if (perSampleInterval < PH_MIN_SAMPLE_SPACING_MS) perSampleInterval = PH_MIN_SAMPLE_SPACING_MS;
    unsigned long lastSampleTime = 0;
        
        // Llenar el array de muestras
        while (sampleCount < PH_ARRAY_LENGTH && (millis() - startTime) < PH_INTERVAL_MS) {
            unsigned long now = millis();
            if (sampleCount == 0 || (now - lastSampleTime) >= perSampleInterval) {
                phArray[sampleCount] = analogRead(sensor_pin);
                sampleCount++;
                lastSampleTime = now;
            } else {
                // Ceder tiempo al scheduler para no bloquear (ESP32-friendly)
                delay(0);
            }
        }
        
        // Calcular promedio descartando extremos
        double avgRaw = averageArray(phArray, sampleCount);
        
        //Convertir a voltaje usando calibración ESP32
        //chat sugiere inconsistencias en la respecto a la resolución entre
        //analogReadResolution(12) pero el esp_adc_cal_characterize() se llamó con ADC_WIDTH_BIT_13
        //mantener 12 bits para evitar errores (pendiente por verificar)
        //ocurre misma discrepancia en initialize donde se usó adc_atten_db_11
        uint32_t voltage_mv = esp_adc_cal_raw_to_voltage((uint32_t)avgRaw, &adc_chars);
        float voltage_v = voltage_mv / 1000.0f;
        
        return voltage_v;
    }
    
    // ——— IMPLEMENTACIÓN DE FUNCIONES PÚBLICAS ———

    /**
     * @brief Inicializa el sensor de pH en el pin ADC especificado
     * @details Configura el ADC con resolución de 12 bits, atenuación de 11dB,
     *          calibración específica del chip ESP32 y limpia buffers de muestras.
     *          Es seguro llamar múltiples veces (verifica si ya está inicializado).
     * @param pin Pin GPIO compatible con ADC1 del ESP32 (ej: GPIO32-39)
     * @return true si inicialización exitosa o ya estaba inicializado
     * @warning Discrepancia en configuración: analogSetPinAttenuation usa ADC_11db pero
     *          esp_adc_cal_characterize usa ADC_ATTEN_DB_12 y ADC_WIDTH_BIT_13.
     *          Se mantiene analogReadResolution(12 bits) por consistencia con código actual.
     *          PENDIENTE: Unificar niveles de atenuación (11dB vs 12dB) y resolución (12 vs 13 bits).
     * @note Imprime información de calibración en Serial si está descomentado.
     */
    
    bool initialize(uint8_t pin) {
        if (initialized) {
            //Serial.println(" Sensor pH ya inicializado");
            return true;
        }
        
        //Serial.printf(" Inicializando sensor pH (pin %d)...\n", pin);
        
        sensor_pin = pin;
        
        // Configurar ADC con calibración ESP32
        //Aquí es donde están las discrepancias en el nivel de atenuación y resolución del adc
        analogReadResolution(ADC_BITS);
        analogSetPinAttenuation(sensor_pin, ADC_11db); // Para voltajes hasta 3.3V
        
        // Calibrar ADC específico para ESP32
        esp_adc_cal_value_t val_type = esp_adc_cal_characterize(
            ADC_UNIT_1,
            ADC_ATTEN_DB_12,
            ADC_WIDTH_BIT_13,
            ADC_VREF,
            &adc_chars
        );
        
        // Limpiar array de muestras
        memset(phArray, 0, sizeof(phArray));
        phArrayIndex = 0;
        
        initialized = true;
        last_reading_time = millis();
        
        //Serial.println(" Sensor pH inicializado correctamente");
        //Serial.printf("   Offset calibrado: %.2f\n", phOffset);
        //Serial.printf("   Pendiente calibrada: %.2f\n", phSlope);

        return true;
    }
    
    /**
     * @brief Limpia y deshabilita el sensor pH
     * @details Marca el sensor como no inicializado, permitiendo reinicialización
     *          posterior. No libera recursos de hardware, solo resetea estado lógico.
     * @note Imprime mensaje de confirmación en Serial si está descomentado.
     */

    void cleanup() {
        initialized = false;
        //Serial.println(" Sensor pH limpiado");
    }
    
    /**
     * @brief Realiza una lectura completa de pH usando temperatura (wrapper)
     * @details Función de conveniencia que llama internamente a takeReadingWithTimeout().
     *          El parámetro temperature actualmente no se utiliza en el cálculo.
     * @param temperature Temperatura del agua en °C (reservado para compensación futura)
     * @return Estructura pHReading con resultado completo de la medición
     * @note La compensación por temperatura NO está implementada. Parámetro reservado.
     */

    pHReading takeReading (float temperature) {
        return takeReadingWithTimeout (temperature);
    }
    
    /**
     * @brief Realiza lectura completa de pH con control de timeout y validación
     * @details Proceso completo: incrementa contador global, lee voltaje promediado,
     *          valida rango de voltaje, convierte a pH usando calibración, valida rango
     *          de pH, actualiza estado y registra errores si es necesario.
     * @param temperature Temperatura del agua en °C (actualmente no usado)
     * @return Estructura pHReading con campos:
     *         - ph_value: Valor de pH calculado (0.0 si inválido)
     *         - voltage: Voltaje medido en voltios
     *         - timestamp: millis() al momento de la lectura
     *         - reading_number: Número secuencial de lectura
     *         - valid: true si lectura válida y dentro de rangos
     *         - sensor_status: Código de estado (OK, TIMEOUT, VOLTAGE_LOW/HIGH, OUT_OF_RANGE)
     * @note Imprime resultados en Serial para debug. Puede descomentarse según necesidad.
     * @note Si hay timeout o valores fuera de rango, decrementa el contador global.
     * @warning El sensor debe estar inicializado antes de llamar esta función.
     */

    pHReading takeReadingWithTimeout(float temperature) {
        pHReading reading = {0};
        
        if (!initialized) {
            //Serial.println(" Sensor pH no inicializado");
            reading.valid = false;
            reading.sensor_status = PH_STATUS_INVALID_READING;
            return reading;
        }
        
        // Incrementar contador
        if (total_readings_counter) {
            (*total_readings_counter)++;
            reading.reading_number = *total_readings_counter;
        }
        
        reading.timestamp = millis();
        
        // Timeout para operación del sensor
        uint32_t start_time = millis();
        
        // Leer voltaje promediado
        float voltage = readAveragedVoltage();
        
        // Verificar timeout
        if (millis() - start_time > PH_OPERATION_TIMEOUT) {
            Serial.println(" Timeout en lectura de sensor pH");
            
            if (error_logger) {
                error_logger(1, 1, millis() - start_time); // ERROR_SENSOR_TIMEOUT
            }
            
            reading.valid = false;
            reading.sensor_status = PH_STATUS_TIMEOUT;
            
            if (total_readings_counter) {
                (*total_readings_counter)--;
            }
            
            last_reading = reading;
            return reading;
        }
        
        // Validar voltaje
        if (!isVoltageInRange(voltage)) {
            if (voltage < MIN_VALID_VOLTAGE) {
                reading.sensor_status = PH_STATUS_VOLTAGE_LOW;
                //Serial.printf(" Voltaje pH muy bajo: %.3fV\n", voltage);
            } else {
                reading.sensor_status = PH_STATUS_VOLTAGE_HIGH;
                //Serial.printf(" Voltaje pH muy alto: %.3fV\n", voltage);
            }
            
            reading.valid = false;
            reading.ph_value = 0.0;
            reading.voltage = voltage;
            
            if (error_logger) {
                error_logger(2, 1, (uint32_t)(voltage * 1000)); // ERROR_SENSOR_INVALID_READING
            }
            
            if (total_readings_counter) {
                (*total_readings_counter)--;
            }
            
            last_reading = reading;
            return reading;
        }
        
        // Calcular pH usando calibración
        float ph = phSlope * voltage + phOffset;
        
        // Validar resultado
        if (isPHInRange(ph)) {
            reading.ph_value = ph;
            reading.voltage = voltage;
            reading.valid = true;
            reading.sensor_status = PH_STATUS_OK;
            
            last_reading_time = millis();
            
            Serial.printf(" pH: %.2f | V: %.3fV | %s (%.0f ms)\n", 
                            ph, voltage, getWaterType(ph).c_str(), millis() - start_time);
        } else {
            reading.ph_value = 0.0;
            reading.voltage = voltage;
            reading.valid = false;
            reading.sensor_status = PH_STATUS_OUT_OF_RANGE;
            
            if (error_logger) {
                error_logger(2, 1, (uint32_t)(ph * 100)); // ERROR_SENSOR_INVALID_READING
            }
            
            if (total_readings_counter) {
                (*total_readings_counter)--;
            }
            
            Serial.printf(" pH fuera de rango: %.2f\n", ph);
        }
        
        last_reading = reading;
        return reading;
    }
    
    // ——— FUNCIONES DE CALIBRACIÓN ———

    /**
     * @brief Establece nuevos parámetros de calibración manualmente
     * @details Actualiza el offset (intercepto) y slope (pendiente) de la ecuación
     *          lineal pH = slope * V + offset. Útil después de calibración externa.
     * @param offset Nuevo valor de offset (intercepto 'b' de la ecuación)
     * @param slope Nueva pendiente (coeficiente 'm' de la ecuación)
     * @note Imprime confirmación de cambios en Serial.
     */
    
    void setCalibration(float offset, float slope) {
        phOffset = offset;
        phSlope = slope;
        Serial.printf(" Calibración pH actualizada: offset=%.2f, pendiente=%.2f\n", 
                        phOffset, phSlope);
    }
    
    /**
     * @brief Obtiene los parámetros de calibración actuales por referencia
     * @details Permite al sistema principal consultar la calibración sin modificarla.
     * @param[out] offset Referencia donde se almacenará el offset actual
     * @param[out] slope Referencia donde se almacenará la pendiente actual
     */
    void getCalibration(float& offset, float& slope) {
        offset = phOffset;
        slope = phSlope;
    }
    
    /**
     * @brief Restablece la calibración a valores por defecto del header
     * @details Restaura phOffset y phSlope a los valores definidos en pH.h como
     *          PH_CALIBRATED_OFFSET y PH_CALIBRATED_SLOPE. Útil para resetear
     *          calibraciones incorrectas o volver a estado de fábrica.
     * @note Imprime confirmación en Serial.
     */

    void resetToDefaultCalibration() {
        phOffset = PH_CALIBRATED_OFFSET;
        phSlope = PH_CALIBRATED_SLOPE;
        Serial.printf(" Calibración pH restaurada a valores por defecto\n");
    }
    
    /**
     * @brief Calibra el sensor usando una solución buffer de pH conocido
     * @details Calibración de un punto: asume pendiente fija y calcula nuevo offset
     *          usando la fórmula: offset = pH_buffer - slope * voltaje_medido.
     *          Ideal para ajuste rápido con buffer pH 7.0.
     * @param bufferPH Valor de pH conocido de la solución buffer (ej: 4.0, 7.0, 10.0)
     * @param measuredVoltage Voltaje medido con el sensor sumergido en el buffer
     * @return true si calibración realizada exitosamente
     * @note Imprime información detallada del proceso en Serial.
     * @note Para calibración más precisa de dos puntos (offset + slope), considerar
     *       implementar función adicional que tome dos buffers diferentes.
     */

    bool calibrateWithBuffer(float bufferPH, float measuredVoltage) {
        // Para calibración simple con un punto (asumiendo pendiente fija)
        // pH = slope * V + offset
        // offset = pH - slope * V
        
        float newOffset = bufferPH - phSlope * measuredVoltage;
        
        Serial.printf(" Calibración con buffer pH %.2f:\n", bufferPH);
        Serial.printf("   Voltaje medido: %.3fV\n", measuredVoltage);
        Serial.printf("   Nuevo offset: %.2f (anterior: %.2f)\n", newOffset, phOffset);
        
        phOffset = newOffset;
        
        return true;
    }
    
    // ——— FUNCIONES DE ESTADO ———
    
    /**
     * @brief Consulta si el sensor está inicializado
     * @return true si initialize() fue llamado exitosamente
     */

    bool isInitialized() { 
        return initialized; 
    }
    
    /**
     * @brief Consulta validez de la última lectura almacenada
     * @return true si last_reading.valid es true
     */
    bool isLastReadingValid() { 
        return last_reading.valid; 
    }
    
    /**
     * @brief Obtiene el valor de pH de la última lectura
     * @return Valor de pH (0.0 si última lectura fue inválida)
     */
    float getLastPH() { 
        return last_reading.ph_value; 
    }
    

    /**
     * @brief Obtiene el voltaje de la última lectura
     * @return Voltaje en voltios
     */
    float getLastVoltage() {
        return last_reading.voltage;
    }
    

    /**
     * @brief Obtiene timestamp de la última lectura válida
     * @return millis() del momento de última lectura exitosa
     */
    uint32_t getLastReadingTime() { 
        return last_reading_time; 
    }
    

    /**
     * @brief Obtiene el total de lecturas realizadas desde el contador global
     * @return Número total de lecturas o 0 si contador no está vinculado
     */
    uint16_t getTotalReadings() {
        return total_readings_counter ? *total_readings_counter : 0;
    }
    
    // ——— FUNCIONES DE UTILIDAD ———
    
    /**
     * @brief Imprime por Serial la última lectura almacenada en formato estructurado
     * @details Muestra: número de lectura, pH, voltaje, timestamp, estado de validez.
     *          Útil para depuración y monitoreo en tiempo real.
     * @note Si no hay lecturas previas (reading_number == 0), informa al usuario.
     */

    void printLastReading() {
        if (last_reading.reading_number == 0) {
            Serial.println(" No hay lecturas pH previas");
            return;
        }
        
        Serial.println(" --- ÚLTIMA LECTURA pH ---");
        Serial.printf("Lectura #%d\n", last_reading.reading_number);
        Serial.printf("pH: %.2f\n", last_reading.ph_value);
        Serial.printf("Voltaje: %.3fV\n", last_reading.voltage);
        Serial.printf("Timestamp: %u ms\n", last_reading.timestamp);
        Serial.printf("Estado: 0x%02X (%s)\n", 
                        last_reading.sensor_status,
                        last_reading.valid ? "VÁLIDA" : "INVÁLIDA");
        Serial.println("---------------------------");
    }
    

    /**
     * @brief Valida si un valor de pH está dentro del rango aceptable
     * @param ph Valor de pH a validar
     * @return true si pH está entre MIN_VALID_PH y MAX_VALID_PH y no es NaN
     */
    bool isPHInRange(float ph) {
        return (ph >= MIN_VALID_PH && ph <= MAX_VALID_PH && !isnan(ph));
    }
    
    /**
     * @brief Valida si un voltaje está dentro del rango aceptable del sensor
     * @param voltage Voltaje en voltios a validar
     * @return true si voltaje está entre MIN_VALID_VOLTAGE y MAX_VALID_VOLTAGE y no es NaN
     */
    bool isVoltageInRange(float voltage) {
        return (voltage >= MIN_VALID_VOLTAGE && voltage <= MAX_VALID_VOLTAGE && !isnan(voltage));
    }
    
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
    String getWaterType(float ph) {
        if (ph < 6.0) return "Muy ácida";
        else if (ph < 6.5) return "Ácida";
        else if (ph < 7.0) return "Ligeramente ácida";
        else if (ph == 7.0) return "Neutra";
        else if (ph < 7.5) return "Ligeramente alcalina";
        else if (ph < 8.5) return "Alcalina";
        else return "Muy alcalina";
    }
    
    // ——— FUNCIONES DE INTEGRACIÓN ———
    
    /**
     * @brief Vincula el sensor con un contador global de lecturas del sistema
     * @param total_readings_ptr Puntero a uint16_t que será incrementado en cada lectura válida
     * @note El puntero debe apuntar a memoria válida durante toda la vida útil del sensor.
     */
    void setReadingCounter(uint16_t* total_readings_ptr) {
        total_readings_counter = total_readings_ptr;
    }
    
    /**
     * @brief Vincula el sensor con un sistema de logging de errores externo
     * @param log_error_func Puntero a función con firma: void(int code, int severity, uint32_t context)
     *        - code: Código de error (1=timeout, 2=lectura inválida, etc.)
     *        - severity: Nivel de severidad (1=warning, 2=error, etc.)
     *        - context: Información contextual (tiempo transcurrido, voltaje*1000, etc.)
     * @note La función debe ser thread-safe si se usa en entorno multitarea.
     */
    void setErrorLogger(void (*log_error_func)(int, int, uint32_t)) {
        error_logger = log_error_func;
    }
    
    // ——— FUNCIONES ADICIONALES ———
    
    /**
     * @brief Muestra información completa de calibración y estado del sensor por Serial
     * @details Imprime:
     *          - Estado de inicialización
     *          - Pin ADC configurado
     *          - Ecuación de calibración actual (pH = slope * V + offset)
     *          - Rangos válidos de pH y voltaje
     *          - Información de última lectura válida si existe
     * @note Útil para verificación rápida de configuración y diagnóstico de problemas.
     */
    void showCalibrationInfo() {
        Serial.println(" === INFORMACIÓN DE CALIBRACIÓN pH ===");
        Serial.printf("Estado: %s\n", initialized ? "Inicializado" : "No inicializado");
        Serial.printf("Pin ADC: %d\n", sensor_pin);
        Serial.printf("Ecuación: pH = %.2f * V + %.2f\n", phSlope, phOffset);
        Serial.printf("Rango válido pH: %.1f - %.1f\n", MIN_VALID_PH, MAX_VALID_PH);
        Serial.printf("Voltaje válido: %.1f - %.1fV\n", MIN_VALID_VOLTAGE, MAX_VALID_VOLTAGE);
        
        if (last_reading.valid) {
            Serial.printf("Última lectura: pH %.2f (%.3fV) - %s\n", 
                            last_reading.ph_value, last_reading.voltage,
                            getWaterType(last_reading.ph_value).c_str());
        } else {
            Serial.println("Sin lecturas válidas recientes");
        }
        Serial.println("=======================================");
    }
    
    /**
     * @brief Realiza una lectura de prueba y muestra resultados detallados
     * @details Lee voltaje promediado, valida rango, calcula pH y muestra cada paso
     *          del proceso. No actualiza last_reading ni contadores globales.
     *          Ideal para verificación rápida sin afectar estadísticas del sistema.
     * @note Requiere sensor inicializado. Imprime resultados en Serial.
     */
    void testReading() {
        if (!initialized) {
            Serial.println(" Sensor no inicializado");
            return;
        }
        
        Serial.println(" === TEST LECTURA pH ===");
        
        float voltage = readAveragedVoltage();
        Serial.printf("Voltaje medido: %.6fV\n", voltage);
        
        if (isVoltageInRange(voltage)) {
            float ph = phSlope * voltage + phOffset;
            
            Serial.printf("pH calculado: %.2f\n", ph);
            Serial.printf("Estado: %s\n", isPHInRange(ph) ? "VÁLIDO" : "FUERA DE RANGO");
        } else {
            Serial.printf(" Voltaje fuera de rango válido (%.1f-%.1fV)\n", 
                            MIN_VALID_VOLTAGE, MAX_VALID_VOLTAGE);
        }
        
        Serial.println("========================");
    }
    
    /**
     * @brief Ejecuta rutina interactiva de calibración con buffer pH 7.0
     * @details Guía paso a paso al usuario para calibrar el sensor:
     *          1. Sumerge sensor en buffer pH 7.0
     *          2. Espera estabilización (30 segundos recomendados)
     *          3. Espera input del usuario en Serial
     *          4. Lee voltaje promediado
     *          5. Calcula y aplica nuevo offset
     * @note Calibración de un punto asumiendo pendiente fija. Para calibración de dos
     *       puntos (ajuste de offset + slope), considerar implementar función extendida.
     * @note Requiere interacción por Serial Monitor. Bloqueante hasta recibir input.
     * @warning Asegurar que sensor esté correctamente sumergido y estabilizado antes de
     *          enviar cualquier carácter por Serial para continuar el proceso.
     */
    void performCalibrationRoutine() {
        if (!initialized) {
            Serial.println(" Sensor no inicializado");
            return;
        }
        
        Serial.println("\n === RUTINA DE CALIBRACIÓN pH ===");
        Serial.println("Necesitarás soluciones buffer de pH conocido");
        Serial.println("Recomendado: pH 4.0, 7.0 y 10.0");
        Serial.println("\n1. Sumerge el sensor en buffer pH 7.0");
        Serial.println("2. Espera 30 segundos para estabilizar");
        Serial.println("3. Presiona cualquier tecla para continuar...");
        
        // Esperar input del usuario
        while (!Serial.available()) {
            delay(100);
        }
        Serial.read(); // Limpiar buffer
        
        Serial.println("\nLeyendo voltaje en pH 7.0...");
        delay(2000);
        
        float voltage7 = readAveragedVoltage();
        Serial.printf("Voltaje en pH 7.0: %.3fV\n", voltage7);
        
        // Calcular nuevo offset asumiendo la pendiente actual
        float newOffset = 7.0 - phSlope * voltage7;
        
        Serial.printf("\nCalibración completada:");
        Serial.printf("  Offset anterior: %.2f\n", phOffset);
        Serial.printf("  Nuevo offset: %.2f\n", newOffset);
        Serial.printf("  Pendiente: %.2f (sin cambios)\n", phSlope);
        
        phOffset = newOffset;
        
        Serial.println("\n Calibración actualizada");
        Serial.println("=====================================");
    }
    
} // namespace pHSensor