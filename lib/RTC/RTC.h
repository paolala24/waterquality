/**
 * @file RTC.h
 * @brief Definición de la clase MAX31328RTC para manejo del RTC MAX31328 en ESP32.
 * 
 * Esta librería proporciona una interfaz simplificada para interactuar con el RTC MAX31328 a través de I2C. 
 * Incluye funciones para inicialización, configuración de fecha/hora, obtención de timestamps Unix, 
 * lectura de temperatura interna, sincronización con NTP y depuración.
 * 
 * @author Daniel Acosta - Santiago Erazo
 * @version 1.0
 * @date 2025-10-01
 */

#ifndef MAX31328RTC_H
#define MAX31328RTC_H

#include <Arduino.h>
#include <Wire.h>
#include <time.h>

// ——— Configuración del MAX31328 ———
#define MAX31328_I2C_ADDRESS    0x68    // Dirección I2C estándar
#define MAX31328_I2C_SPEED      100000  // 100kHz para mayor compatibilidad

// ——— Registros del MAX31328 ———
#define MAX31328_REG_SECONDS    0x00
#define MAX31328_REG_MINUTES    0x01
#define MAX31328_REG_HOURS      0x02
#define MAX31328_REG_WEEKDAY    0x03
#define MAX31328_REG_DAY        0x04
#define MAX31328_REG_MONTH      0x05
#define MAX31328_REG_YEAR       0x06
#define MAX31328_REG_CONTROL    0x0E
#define MAX31328_REG_STATUS     0x0F
#define MAX31328_REG_TEMP_MSB   0x11
#define MAX31328_REG_TEMP_LSB   0x12

// ——— Bits de control importantes ———
#define MAX31328_CTRL_EOSC      0x80    // Enable Oscillator 
#define MAX31328_STAT_OSF       0x80    // Oscillator Stop Flag

/**
 * @brief Clase simplificada para MAX31328 RTC compatible con ESP32
 * Implementa funciones de configuración y lectura de tiempo, 
 * Lectura de temperatura interna, sincronización NTP y depuración.
 * Basada en el datasheet oficial y optimizada para uso con sensores de agua
 */
class MAX31328RTC {
private:
    uint8_t i2c_address;
    bool initialized;
    
    // Funciones auxiliares para conversión BCD
    uint8_t decToBcd(uint8_t val);
    uint8_t bcdToDec(uint8_t val);
    
    // Comunicación I2C
    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    bool readMultipleRegisters(uint8_t startReg, uint8_t* buffer, uint8_t length);
    bool writeMultipleRegisters(uint8_t startReg, uint8_t* buffer, uint8_t length);

public:
    /**
     * @brief Constructor de la clase
     */
    MAX31328RTC();
    
    /**
     * @brief Inicializar el RTC con pines I2C específicos
     * @param sda_pin Pin SDA para I2C
     * @param scl_pin Pin SCL para I2C
     * @param address Dirección I2C (default: 0x68)
     * @return true si la inicialización fue exitosa
     */
    bool begin(int sda_pin = 21, int scl_pin = 22, uint8_t address = MAX31328_I2C_ADDRESS);
    
    /**
     * @brief Verificar si el RTC está presente y funcionando
     * @return true si el RTC responde correctamente
     */
    bool isPresent();
    
    /**
     * @brief Verificar si el RTC está funcionando (oscilador activo)
     * @return true si el oscilador está funcionando
     */
    bool isRunning();
    
    /**
     * @brief Configurar fecha y hora
     * @param year Año (2000-2099)
     * @param month Mes (1-12)
     * @param day Día (1-31)
     * @param hour Hora (0-23)
     * @param minute Minuto (0-59)
     * @param second Segundo (0-59)
     * @return true si se configuró correctamente
     */
    bool setDateTime(uint16_t year, uint8_t month, uint8_t day, 
                        uint8_t hour, uint8_t minute, uint8_t second);
    
    /**
     * @brief Obtener fecha y hora actual
     * @param year Referencia para almacenar el año
     * @param month Referencia para almacenar el mes
     * @param day Referencia para almacenar el día
     * @param hour Referencia para almacenar la hora
     * @param minute Referencia para almacenar el minuto
     * @param second Referencia para almacenar el segundo
     * @return true si la lectura fue exitosa
     */
    bool getDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                        uint8_t& hour, uint8_t& minute, uint8_t& second);
    
    /**
     * @brief Obtener timestamp Unix
     * @return Timestamp Unix (segundos desde 1970) o 0 si hay error
     */
    uint32_t getUnixTimestamp();
    
    /**
     * @brief Configurar fecha/hora desde timestamp Unix
     * @param timestamp Timestamp Unix
     * @return true si se configuró correctamente
     */
    bool setUnixTimestamp(uint32_t timestamp);
    
    /**
     * @brief Obtener temperatura del RTC
     * @return Temperatura en °C o -999.0 si hay error
     */
    float getTemperature();
    
    /**
     * @brief Obtener fecha/hora como string formateado
     * @return String con formato "YYYY-MM-DD HH:MM:SS"
     */
    String getFormattedDateTime();
    
    /**
     * @brief Iniciar el oscilador si está detenido
     * @return true si se inició correctamente
     */
    bool startOscillator();
    
    /**
     * @brief Verificar si el RTC perdió la hora (flag OSF)
     * @return true si el RTC perdió la hora
     */
    bool hasLostTime();
    
    /**
     * @brief Limpiar el flag de tiempo perdido
     * @return true si se limpió correctamente
     */
    bool clearLostTimeFlag();
    
    /**
     * @brief Sincronizar con NTP (requiere WiFi activo)
     * @param ntpServer Servidor NTP
     * @param gmtOffset Offset GMT en horas
     * @return true si se sincronizó correctamente
     */
    bool syncWithNTP(const char* ntpServer = "pool.ntp.org", int gmtOffset = -5);
    
    /**
     * @brief Mostrar información de debug por Serial
     */
    void printDebugInfo();
    
    /**
     * @brief Mostrar registros principales por Serial
     */
    void printRegisters();
};

#endif // MAX31328RTC_H