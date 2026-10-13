/**
 * @file Temperatura.cpp
 * @brief Implementación del sensor de temperatura DS18B20 para ESP32
 * @details Este archivo contiene la lógica completa para inicialización, lectura
 *          y validación del sensor digital de temperatura DS18B20 usando protocolo
 *          OneWire. Implementa control de timeout, validación de rangos y gestión
 *          dinámica de memoria para los objetos de comunicación.
 * @author Daniel Acosta - Santiago Erazo
 * @date 01/10/2025
 * @version 1.0
 */

#include "Temperatura.h"



/**
 * @namespace TemperatureSensor
 * @brief Espacio de nombres para todas las funcionalidades del sensor de temperatura
 * @details Contiene variables globales internas, funciones de lectura, validación
 *          y utilidades para manejo completo del sensor DS18B20 conectado mediante
 *          protocolo OneWire al ESP32.
 */
namespace TemperatureSensor {

    // ——— Variables internas del módulo ———
    
    /**
     * @brief Puntero al objeto OneWire para comunicación con el bus 1-Wire
     * @details Gestiona el protocolo de comunicación de bajo nivel con el sensor DS18B20.
     *          Se crea dinámicamente en initialize() y se libera en cleanup().
     * @note nullptr cuando no está inicializado. Verificar antes de usar.
     */ 
    OneWire* oneWire = nullptr;

    /**
     * @brief Puntero al objeto DallasTemperature para gestión del sensor DS18B20
     * @details Proporciona API de alto nivel para solicitar y leer temperaturas del
     *          sensor DS18B20. Se crea dinámicamente en initialize() sobre oneWire.
     * @note nullptr cuando no está inicializado. Verificar antes de usar.
     */
    DallasTemperature* sensors = nullptr;

    /**
     * @brief Bandera de estado de inicialización del sensor de temperatura
     * @details Indica si initialize() fue llamado exitosamente y los objetos OneWire
     *          y DallasTemperature fueron creados correctamente.
     */
    bool initialized = false;

    /**
     * @brief Timestamp de la última lectura válida realizada
     * @details Almacena millis() del momento en que se completó exitosamente una
     *          lectura. Útil para calcular intervalos entre mediciones.
     */
    uint32_t last_reading_time = 0;

    /**
     * @brief Última estructura de lectura capturada por el sensor
     * @details Contiene temperatura, timestamp, estado y número de lectura.
     *          Se actualiza en cada llamada a takeReadingWithTimeout().
     */
    TemperatureReading last_reading = {0};
    
    // Punteros a funciones externas

    /**
     * @brief Puntero al contador global de lecturas del sistema
     * @details Permite incrementar un contador externo cada vez que se realiza una
     *          lectura válida. nullptr si no se ha vinculado con sistema principal.
     */
    uint16_t* total_readings_counter = nullptr;

    /**
     * @brief Puntero a función de logging de errores del sistema
     * @details Callback para reportar errores (timeout, lectura inválida, etc.) al
     *          sistema principal. nullptr si no está configurado.
     */
    void (*error_logger)(int code, int severity, uint32_t context) = nullptr;
    
    
    
    // ——— Implementación de funciones ———
    
    /**
     * @brief Inicializa el sensor de temperatura DS18B20 en el pin especificado
     * @details Crea dinámicamente objetos OneWire y DallasTemperature, configura el
     *          bus 1-Wire y prepara el sensor para lecturas. Es seguro llamar múltiples
     *          veces (verifica si ya está inicializado). Gestiona memoria dinámicamente.
     * @param pin Pin GPIO del ESP32 para comunicación OneWire (por defecto TEMP_SENSOR_PIN)
     * @return true si inicialización exitosa o ya estaba inicializado, false si error
     * @note Requiere llamarse una vez en setup() antes de usar otras funciones.
     * @warning Si falla la creación de objetos, libera memoria automáticamente y retorna false.
     * @note El sensor DS18B20 requiere resistencia pull-up de 4.7kΩ en el bus OneWire.
     */
    bool initialize(uint8_t pin) { // Función pública que inicializa los objetos OneWire y DallasTemperature usando el pin indicado.
        if (initialized) { // Si ya fue inicializado, evitar reinicializar.
            //Serial.println(" Sensor temperatura ya inicializado");
            return true; // Retornar true porque ya está inicializado.

        }
        
        //Serial.printf(" Inicializando sensor temperatura (pin %d)...\n", pin);
        
        // Crear objetos 
        oneWire = new OneWire(pin); // Reserva dinámicamente un objeto OneWire asociado al pin pasado.
        if (!oneWire) { // Si la asignación falló (puntero nulo)...
            Serial.println(" Error creando OneWire"); // Informar por consola.
            return false; // Retornar false para indicar fallo en inicialización.
        }
        
        sensors = new DallasTemperature(oneWire); // Crea el wrapper DallasTemperature que usa el bus OneWire.
        if (!sensors) { // Si no se pudo crear el wrapper...
            Serial.println(" Error creando DallasTemperature"); // Imprime error.
            delete oneWire;  // Libera el objeto OneWire para evitar fuga de memoria.
            oneWire = nullptr; // Establece puntero a nulo por seguridad.
            return false; // Retorna false por falla.
        }
        
        // Inicializar sensor
        sensors->begin(); // Inicializa internamente la librería DallasTemperature (detecta dispositivos, prepara bus).

        // Evita que requestTemperatures() espere internamente; la conversión se controla con isConversionComplete().
        sensors->setWaitForConversion(false);
        // Se conserva la resolución predeterminada del sensor.
        
        initialized = true; // Marca el módulo como inicializado.
        last_reading_time = millis(); // Asigna el tiempo actual (ms) como tiempo de referencia para última lectura.
        
        //Serial.println(" Sensor temperatura inicializado correctamente");
        return true; // Retorna true indicando inicialización exitosa.
    }
    
    /**
     * @brief Limpia y libera recursos del sensor de temperatura
     * @details Elimina dinámicamente los objetos DallasTemperature y OneWire,
     *          liberando memoria heap. Marca el sensor como no inicializado.
     * @note Es seguro llamar aunque no esté inicializado (verifica punteros).
     * @note Útil para reset de sistema o cambio de configuración.
     */
    void cleanup() { // Función para liberar recursos y poner el módulo en estado limpio/no inicializado.
        if (sensors) { // Si existe objeto DallasTemperature...
            delete sensors; // Libera el objeto.
            sensors = nullptr; // Evita dangling pointer (puntero colgante).
        }
        if (oneWire) {  // Si existe objeto OneWire...
            delete oneWire; // Libera el objeto.
            oneWire = nullptr; // Evita puntero colgante.
        }
        initialized = false; // Marca el módulo como no inicializado.
        Serial.println(" Sensor temperatura limpiado"); // Imprime mensaje informativo.
    }
    
    /**
     * @brief Realiza una lectura de temperatura (wrapper de takeReadingWithTimeout)
     * @details Función de conveniencia que llama internamente a takeReadingWithTimeout().
     * @return Estructura TemperatureReading con resultado completo de la medición
     * @see takeReadingWithTimeout() para detalles completos del proceso de lectura
     */
    TemperatureReading takeReading() { // Función pública que simplifica la llamada devolviendo la versión con timeout por defecto.
        return takeReadingWithTimeout(); // Llama a la función que implementa timeout (la versión completa).
    }
    
    /**
     * @brief Realiza lectura de temperatura con control de timeout y validación exhaustiva
     * @details Función principal para toma de datos. Proceso completo:
     *          1. Verifica inicialización del sensor y punteros válidos
     *          2. Incrementa contador global de lecturas
     *          3. Solicita conversión de temperatura al DS18B20 (requestTemperatures)
    *          4. Espera completitud de conversión con polling de 5ms
     *          5. Verifica timeout de operación (< TEMP_OPERATION_TIMEOUT)
     *          6. Lee temperatura en °C usando getTempCByIndex(0)
     *          7. Valida rango de temperatura y detecta desconexión (DEVICE_DISCONNECTED_C)
     *          8. Actualiza last_reading y registra errores si corresponde
     * @return Estructura TemperatureReading con campos:
     *         - temperature: Temperatura en °C (0.0 si inválida)
     *         - timestamp: millis() al momento de la lectura
     *         - reading_number: Número secuencial de lectura
     *         - valid: true si lectura válida y dentro de rangos
     *         - sensor_status: Código bit-field de estado (ver TEMP_STATUS_*)
     * @note Si hay timeout o valores fuera de rango, decrementa el contador global.
     * @note La conversión del DS18B20 tarda típicamente 750ms con resolución de 12 bits.
    * @warning La solicitud de conversión no bloquea; esta función espera mediante polling
    *          hasta completar la conversión o alcanzar el timeout.
     */
    TemperatureReading takeReadingWithTimeout() { // Función que realiza la lectura real con manejo de errores y timeout.
        TemperatureReading reading = {0}; // Inicializa la estructura de lectura a ceros (timestamp, temperatura, flags...).
        
        if (!initialized || !sensors) { // Comprueba que el módulo esté correctamente inicializado y los objetos existan.
            Serial.println(" Sensor temperatura no inicializado"); // Mensaje de error si no está listo.
            reading.valid = false; // Marca la lectura como inválida.
            reading.sensor_status = TEMP_STATUS_INVALID_READING; // Establece el código de estado indicando error.
            return reading; // Retorna la estructura con invalid flag.
        }
        
        // Incrementar contador 
        if (total_readings_counter) { // Si el sistema principal ha proporcionado un puntero al contador global...
            (*total_readings_counter)++; // Incrementa el contador global en 1.
            reading.reading_number = *total_readings_counter; // Guarda el número de lectura actual en la estructura.
        }
        
        reading.timestamp = millis(); // Registra la marca de tiempo (ms) de inicio de la lectura.
        
        // Timeout para operación completa
        uint32_t start_time = millis(); // Marca el tiempo de inicio para comprobar timeout luego.

        // Iniciar conversión sin espera interna
        sensors->requestTemperatures();

        // Esperar finalización con timeout
        while (!sensors->isConversionComplete()) {
            uint32_t elapsed = millis() - start_time;

            if (elapsed > TEMP_OPERATION_TIMEOUT) {
                Serial.println(" Timeout en lectura de sensor"); // Imprime mensaje de timeout.
                
                if (error_logger) { // Si hay función registrada para logging de errores...
                    error_logger(1, 1, elapsed); // ERROR_SENSOR_TIMEOUT, SEVERITY_WARNING
                    // Llama al logger con código 1 (timeout) y severidad 1.
                }
                
                reading.temperature = 0.0;
                reading.valid = false; // Marca la lectura como inválida por timeout.
                reading.sensor_status = TEMP_STATUS_TIMEOUT;   // Establece el estado de timeout.
                
                if (total_readings_counter) { // Si se incrementó el contador al inicio...
                    (*total_readings_counter)--; // Revertir el incremento para no contar lecturas fallidas.
                }
                
                last_reading = reading; // Guarda la lectura (inválida) como última lectura para trazabilidad.
                return reading; // Retorna la lectura inválida por timeout.
            }
            
            delay(5);

            #if defined(ESP32)
            yield();
            #endif
        }

        // Conversión terminada
        float tempC = sensors->getTempCByIndex(0); // Obtiene la temperatura del primer sensor en el bus (índice 0).
        
        // Validar lectura
        if (tempC != DEVICE_DISCONNECTED_C && isTemperatureInRange(tempC)) {
            // Comprueba que el sensor no esté desconectado y que la temperatura esté dentro de límites razonables.
            reading.temperature = tempC; // Almacena la temperatura medida en la estructura.
            reading.valid = true; // Marca la lectura como válida.
            reading.sensor_status = TEMP_STATUS_OK; // Estado OK.
            
            last_reading_time = millis(); // Actualiza la marca de tiempo del último dato válido.
            Serial.printf(" Temperatura: %.2f °C (%.0f ms)\n", tempC, millis() - start_time);
            // Imprime la temperatura con 2 decimales y el tiempo que tardó la operación.
        } else {
            reading.temperature = 0.0; // En caso de lectura inválida, pone temperatura a 0.0 para indicar fallo.
            reading.valid = false; // Marca lectura inválida.
            reading.sensor_status = TEMP_STATUS_INVALID_READING;   // Estado de lectura inválida.
            
            if (error_logger) { // Si hay un logger definido...
                error_logger(2, 1, (uint32_t)(tempC * 100)); // ERROR_SENSOR_INVALID_READING, SEVERITY_WARNING
                // Reporta error tipo 2 (lectura inválida), severidad 1, contexto: tempC*100.
            }
            
            // Revertir incremento 
            if (total_readings_counter) { // Si se incrementó el contador antes...
                (*total_readings_counter)--; // Revierte el incremento porque la lectura no es válida.
            }
            
            Serial.printf(" Lectura inválida: %.2f °C\n", tempC); // Imprime por consola la lectura inválida para diagnóstico.
        }
        
        // Guardar última lectura
        last_reading = reading; // Guarda la estructura 'reading' en la variable global 'last_reading' para consulta posterior.
        
        return reading; // Devuelve la lectura (válida o inválida) al llamador.
    }
    
    /**
     * @brief Consulta si el sensor está inicializado y listo para uso
     * @return true si initialize() fue llamado exitosamente
     */
    bool isInitialized() { // Función que indica si el módulo fue inicializado correctamente.
        return initialized; // Devuelve la bandera 'initialized'.
    }
    
    /**
     * @brief Consulta validez de la última lectura almacenada
     * @return true si last_reading.valid es true
     */
    bool isLastReadingValid() { // Indica si la última lectura registrada fue considerada válida.
        return last_reading.valid; // Lee el campo 'valid' de la estructura 'last_reading'.
    }
    
    /**
     * @brief Obtiene el valor de temperatura de la última lectura
     * @return Temperatura en °C (0.0 si última lectura fue inválida)
     */
    float getLastTemperature() { // Devuelve la última temperatura registrada por el sensor.
        return last_reading.temperature; // Retorna el valor almacenado en 'last_reading.temperature'.
    }
    
    /**
     * @brief Obtiene timestamp de la última lectura válida
     * @return millis() del momento de última lectura exitosa
     */
    uint32_t getLastReadingTime() { // Devuelve el timestamp (ms) de la última lectura válida.
        return last_reading_time; // Retorna la variable 'last_reading_time'.
    }
    
    /**
     * @brief Obtiene el total de lecturas realizadas desde el contador global
     * @return Número total de lecturas o 0 si contador no está vinculado
     */
    uint16_t getTotalReadings() { // Devuelve el total de lecturas acumuladas si existe el contador global.
        return total_readings_counter ? *total_readings_counter : 0;
        // Si 'total_readings_counter' es distinto de nulo devuelve su valor; si no, devuelve 0.
    }
    
    /**
     * @brief Imprime por Serial la última lectura almacenada en formato estructurado
     * @details Muestra: número de lectura, temperatura, timestamp, estado de validez.
     *          Útil para depuración y monitoreo en tiempo real.
     * @note Si no hay lecturas previas (reading_number == 0), informa al usuario.
     */
    void printLastReading() { // Función utilitaria para imprimir por consola los detalles de la última lectura.
        if (last_reading.reading_number == 0) { // Función utilitaria para imprimir por consola los detalles de la última lectura.
            Serial.println(" No hay lecturas previas");
            return; // Sale de la función.
        }
        
        Serial.println(" --- ÚLTIMA LECTURA TEMPERATURA ---");
        Serial.printf("Lectura #%d\n", last_reading.reading_number); // Imprime el número de lectura.
        Serial.printf("Temperatura: %.2f °C\n", last_reading.temperature); // Imprime temperatura
        Serial.printf("Timestamp: %u ms\n", last_reading.timestamp); // Imprime el timestamp (ms).
        Serial.printf("Estado: 0x%02X (%s)\n", 
                        last_reading.sensor_status,
                        last_reading.valid ? "VÁLIDA" : "INVÁLIDA");
        Serial.println("---------------------------------------");
    }
    
    /**
     * @brief Valida si una temperatura está dentro del rango aceptable
     * @param temp Temperatura a validar en °C
     * @return true si temp está entre MIN_VALID_TEMP (-50°C) y MAX_VALID_TEMP (85°C) y no es NaN
     * @note Rango basado en especificaciones del sensor DS18B20 (-55°C a +125°C),
     *       ajustado a rangos prácticos para aplicaciones de monitoreo de agua.
     */
    bool isTemperatureInRange(float temp) { // Comprueba si una temperatura dada está dentro de los límites permitidos.
        return (temp > MIN_VALID_TEMP && temp < MAX_VALID_TEMP && !isnan(temp));
        // Devuelve true si temp está entre MIN_VALID_TEMP y MAX_VALID_TEMP y no es NaN.
    }
    
    /**
     * @brief Vincula el sensor con un contador global de lecturas del sistema
     * @details Permite que el módulo de temperatura incremente automáticamente un
     *          contador externo en cada lectura válida. Útil para estadísticas globales.
     * @param total_readings_ptr Puntero a uint16_t que será incrementado en cada lectura válida
     * @note El puntero debe apuntar a memoria válida durante toda la vida útil del sensor.
     * @warning No pasar punteros a variables locales que puedan salir de scope.
     */
    void setReadingCounter(uint16_t* total_readings_ptr) { // Permite al sistema principal pasar un puntero al contador global.
        total_readings_counter = total_readings_ptr; // Asigna el puntero al campo interno 'total_readings_counter'.
    }
    
    /**
     * @brief Vincula el sensor con un sistema de logging de errores externo
     * @details Permite que el módulo de temperatura reporte errores (timeout, lectura
     *          inválida, etc.) a un sistema centralizado de gestión de errores o logger.
     * @param log_error_func Puntero a función con firma: void(int code, int severity, uint32_t context)
     *        - code: Código de error (1=timeout, 2=lectura inválida, etc.)
     *        - severity: Nivel de severidad (1=warning, 2=error, 3=crítico, etc.)
     *        - context: Información contextual (tiempo transcurrido, temperatura*100, etc.)
     * @note La función debe ser thread-safe si se usa en entorno multitarea (FreeRTOS).
     * @warning No pasar punteros a funciones lambda sin captura estática.
     */
    void setErrorLogger(void (*log_error_func)(int, int, uint32_t)) { // Permite registrar el callback para logging de errores.
        error_logger = log_error_func; // Guarda el puntero a la función de logging en 'error_logger'.
    }
    
} // namespace TemperatureSensor 