/**
 * @file RTC.cpp
 * @brief Implementación de la clase MAX31328RTC para manejo del RTC mediante I2C.
 * 
 * Este archivo contiene la implementación de la clase MAX31328RTC, que provee
 * funciones para inicialización, verificación, lectura y escritura de fecha y hora,
 * sincronización con NTP, obtención de temperatura interna, depuración y utilidades.
 * 
 * @version 1.0
 * @date 2025-10-01
 * @author Daniel Acosta - Santiago Erazo
 */

#include "RTC.h"

/**
 * @brief Constructor de la clase MAX31328RTC.
 * 
 * Inicializa valores por defecto para dirección I2C y estado de inicialización.
 */
MAX31328RTC::MAX31328RTC() {
    i2c_address = MAX31328_I2C_ADDRESS;
    initialized = false;
}

/**
 * @brief Inicializa el dispositivo RTC en el bus I2C.
 * 
 * Configura los pines, velocidad I2C y verifica presencia y oscilador.
 * 
 * @param sda_pin Pin SDA.
 * @param scl_pin Pin SCL.
 * @param address Dirección I2C del dispositivo.
 * @return true si la inicialización fue exitosa.
 * @return false si hubo error.
 */
bool MAX31328RTC::begin(int sda_pin, int scl_pin, uint8_t address) {
    // Función de inicio: configura la comunicación I2C y valida la presencia del RTC.
    i2c_address = address; // Se guarda la dirección I2C indicada por el usuario o por defecto.
    
    //Serial.printf("MAX31328: Inicializando I2C (SDA=%d, SCL=%d, Addr=0x%02X)\n", 
                  //sda_pin, scl_pin, i2c_address);
    
    // Inicializar I2C con pines específicos
    Wire.end(); // Terminar sesión previa si existe (// Finaliza cualquier comunicación I2C previa para reiniciar la conexión limpia.)
    delay(100); // Pequeña pausa para estabilizar el bus I2C después de cerrarlo.
    
    if (!Wire.begin(sda_pin, scl_pin)) { // Intenta iniciar la comunicación I2C en los pines especificados.
        Serial.println("MAX31328: Error inicializando I2C"); // Muestra error si no logra inicializar.
        return false; // Devuelve falso para indicar que no se pudo inicializar.
    }
    
    // Configurar velocidad I2C (100kHz para mayor compatibilidad)
    Wire.setClock(MAX31328_I2C_SPEED); // Configura la velocidad de transmisión I2C (ejemplo: 100kHz para mayor compatibilidad).
    delay(200); // Dar tiempo para estabilización
    
    // Verificar presencia del dispositivo
    if (!isPresent()) { // Verifica si el dispositivo está presente en el bus I2C.
        Serial.println("MAX31328: Dispositivo no detectado en I2C"); // Si no se encuentra, muestra error.
        return false; // Retorna falso porque el RTC no está conectado o no responde.
    }
    
    Serial.println("MAX31328: Dispositivo detectado correctamente"); // Mensaje indicando que el RTC respondió bien.
    
    // Verificar si el oscilador está funcionando
    if (!isRunning()) { // Verifica si el oscilador interno del RTC está funcionando.
        //Serial.println("MAX31328: Oscilador detenido - Iniciando...");
        if (!startOscillator()) { // Intenta iniciar el oscilador si estaba apagado.
            Serial.println("MAX31328: Error iniciando oscilador"); // Si falla, muestra mensaje de error.
            return false; // Retorna falso indicando que no se pudo iniciar el oscilador.
        }
        
    }
    
    initialized = true; // Marca el dispositivo como inicializado exitosamente.
    Serial.println("MAX31328: Inicialización completada"); // Mensaje de éxito en la inicialización.
    
    return true; // Devuelve verdadero, indicando que el RTC está listo para usarse.
}

/**
 * @brief Verifica si el dispositivo está presente en el bus I2C.
 * @return true si responde en la dirección I2C.
 * @return false si no responde o hay error.
 */

bool MAX31328RTC::isPresent() {
    Wire.beginTransmission(i2c_address);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) { // Si no hubo error, significa que el dispositivo respondió correctamente
        // Intentar leer el registro de estado para confirmar
        uint8_t status = readRegister(MAX31328_REG_STATUS); // Lee el registro de estado del RTC
        
        if (status == 0xFF) { // 0xFF indica una lectura inválida (error de comunicación o sin datos)
            Serial.println("MAX31328: Registro de estado inválido (0xFF)"); // Mensaje de error por puerto serie
            return false; // El dispositivo no respondió correctamente
        }
        
        Serial.printf("MAX31328: Dispositivo presente - Status: 0x%02X\n", status); // Imprime el estado leído  
        return true; // Retorna que el dispositivo está presente
    }
    
    Serial.printf("MAX31328: Error I2C: %d\n", error); // Si hubo error en la comunicación I2C, imprime el código
    switch (error) { // Evalúa el tipo de error según el valor devuelto
        case 2: // Error código 2
            Serial.println("MAX31328: NACK en dirección - dispositivo no responde"); // No hubo ACK en la dirección
            break;
        case 4: // Error código 4
            Serial.println("MAX31328: Error desconocido en I2C"); // Se detectó un error no identificado
            break;
        case 5: // Error código 5
            Serial.println("MAX31328: Timeout en I2C"); // Tiempo de espera agotado en la comunicación
            break;
        default: // Cualquier otro error no documentado
            Serial.printf("MAX31328: Error I2C no documentado: %d\n", error); // Muestra el error tal cual
            break;
    }
    
    return false; // Si llegamos aquí, el dispositivo no está presente o no respondió bien
}

/**
 * @brief Verifica si el oscilador está en funcionamiento.
 * @return true si está corriendo, false si está detenido.
 */

bool MAX31328RTC::isRunning() {
    if (!initialized && !isPresent()) {
        return false;
    }
    
    uint8_t status = readRegister(MAX31328_REG_STATUS); // Lee el registro de estado del RTC
    
    // El bit OSF (bit 7) indica si el oscilador se detuvo
    bool running = !(status & MAX31328_STAT_OSF); // running será true si el bit OSF está en 0 (oscilador activo)
    
    Serial.printf("MAX31328: Oscilador %s (Status: 0x%02X)\n", 
                running ? "funcionando" : "detenido", status);
    
    return running; // Retorna el estado del oscilador
}

/**
 * @brief Inicia el oscilador del RTC.
 * @return true si se inició correctamente.
 */

bool MAX31328RTC::startOscillator() {
    Serial.println("MAX31328: Iniciando oscilador...");
    
    // Leer registro de control
    uint8_t control = readRegister(MAX31328_REG_CONTROL); // Se obtiene el valor actual del registro de control
    Serial.printf("MAX31328: Control actual: 0x%02X\n", control); // Se imprime el valor leído
    
    // Habilitar oscilador (EOSC = 0). Se limpia el bit de apagado del oscilador
    control &= ~MAX31328_CTRL_EOSC; // Apaga el bit EOSC para activar el oscilador
    
    if (!writeRegister(MAX31328_REG_CONTROL, control)) { // Escribe el nuevo valor en el registro de control
        Serial.println("MAX31328: Error escribiendo registro de control"); // Si falla, muestra error
        return false; // Y retorna false
    }
    
    // Limpiar flag OSF
    if (!clearLostTimeFlag()) { // Se intenta borrar el flag que indica pérdida de tiempo
        Serial.println("MAX31328: Error limpiando flag OSF"); // Error al limpiar el flag
        return false; // Retorna falso si no pudo
    }
    
    delay(1000); // Dar tiempo para que se estabilice. Se espera 1 segundo para que el oscilador se estabilice
    
    return isRunning(); // Se comprueba y retorna si el oscilador ya está corriendo
}

/**
 * @brief Indica si el RTC perdió el tiempo (flag OSF activo).
 * @return true si perdió el tiempo.
 */

bool MAX31328RTC::hasLostTime() {
    uint8_t status = readRegister(MAX31328_REG_STATUS);
    return (status & MAX31328_STAT_OSF) != 0;
}

/**
 * @brief Limpia el flag de pérdida de tiempo (OSF).
 * @return true si fue exitoso.
 */

bool MAX31328RTC::clearLostTimeFlag() {
    uint8_t status = readRegister(MAX31328_REG_STATUS);
    status &= ~MAX31328_STAT_OSF; // Limpiar bit OSF
    return writeRegister(MAX31328_REG_STATUS, status);
}

/**
 * @brief Configura fecha y hora manualmente.
 * @param year Año (2000-2099).
 * @param month Mes (1-12).
 * @param day Día (1-31).
 * @param hour Hora (0-23).
 * @param minute Minuto (0-59).
 * @param second Segundo (0-59).
 * @return true si se configuró correctamente.
 */

bool MAX31328RTC::setDateTime(uint16_t year, uint8_t month, uint8_t day, 
                            uint8_t hour, uint8_t minute, uint8_t second) {
    
    if (!initialized && !isPresent()) { // Verifica que el dispositivo esté inicializado o presente
        Serial.println("MAX31328: RTC no inicializado"); // Mensaje de error si no lo está
        return false; // No continuar si no está listo
    }
    
    // Validar rangos
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || 
        day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) { // Comprueba límites razonables
        Serial.println("MAX31328: Fecha/hora fuera de rango"); // Log si alguno está fuera de rango
        return false; // Rechaza configuración inválida
    }
    
    Serial.printf("MAX31328: Configurando %04d-%02d-%02d %02d:%02d:%02d\n",
                year, month, day, hour, minute, second);
    
    // Preparar datos en formato BCD
    uint8_t timeRegs[7]; // Buffer con los 7 registros (seg, min, hora, diaSem, dia, mes, año)
    timeRegs[0] = decToBcd(second); // Segundos en BCD
    timeRegs[1] = decToBcd(minute); // Minutos en BCD
    timeRegs[2] = decToBcd(hour);   // Hora en BCD     
    timeRegs[3] = 1;                // Día de la semana: se coloca 1 (valor no crítico en este uso)                    
    timeRegs[4] = decToBcd(day);    // Día del mes en BCD
    timeRegs[5] = decToBcd(month);  // Mes en BCD
    timeRegs[6] = decToBcd(year - 2000); // Año offset 2000 en BCD
    
    // Escribir todos los registros de tiempo
    if (!writeMultipleRegisters(MAX31328_REG_SECONDS, timeRegs, 7)) { // Escribe desde registro SECONDS los 7 bytes
        Serial.println("MAX31328: Error escribiendo fecha/hora"); // Error si falla escritura
        return false; // Indica fallo
    }
    
    // Limpiar flag OSF
    clearLostTimeFlag(); // Limpia el flag de pérdida de tiempo después de configurar fecha/hora
    
    Serial.println("MAX31328: Fecha/hora configurada correctamente"); // Confirmación
    return true; // Operación exitosa
}

/**
 * @brief Obtiene la fecha y hora actual.
 * @param year Referencia para año.
 * @param month Referencia para mes.
 * @param day Referencia para día.
 * @param hour Referencia para hora.
 * @param minute Referencia para minuto.
 * @param second Referencia para segundo.
 * @return true si la lectura fue exitosa.
 */

bool MAX31328RTC::getDateTime(uint16_t& year, uint8_t& month, uint8_t& day,
                            uint8_t& hour, uint8_t& minute, uint8_t& second) {
    
    if (!initialized && !isPresent()) {
        return false;
    }
    
    // Leer registros de tiempo
    uint8_t timeRegs[7];
    if (!readMultipleRegisters(MAX31328_REG_SECONDS, timeRegs, 7)) { // Leer varios registros desde SECONDS
        Serial.println("MAX31328: Error leyendo fecha/hora"); // Error de lectura
        return false; // Retorna fallo
    }
    
    // Convertir de BCD a decimal
    second = bcdToDec(timeRegs[0] & 0x7F);  // Segundos: mask 0x7F para limpiar flag de reloj
    minute = bcdToDec(timeRegs[1] & 0x7F);  // Minutos
    hour = bcdToDec(timeRegs[2] & 0x3F);    // Hora: mask 0x3F para manejar formato 24h/12h
    day = bcdToDec(timeRegs[4] & 0x3F);     // Día del mes
    month = bcdToDec(timeRegs[5] & 0x1F);   // Mes: mask 0x1F por si hay flags en los bits superiores
    year = 2000 + bcdToDec(timeRegs[6]);    // Año: se suma 2000 al BCD almacenado (offset)
    
    return true;
}

/**
 * @brief Obtiene el timestamp Unix (UTC) desde RTC.
 * @return timestamp Unix (segundos) o 0 en caso de error.
 */

uint32_t MAX31328RTC::getUnixTimestamp() {
    uint16_t year;
    uint8_t month, day, hour, minute, second; // Variables locales para recibir fecha/hora del RTC
    
    if (!getDateTime(year, month, day, hour, minute, second)) { // Llama a getDateTime; si falla...
        return 0; // Retorna 0 para indicar error
    }
    
    // Convertir a timestamp Unix
    struct tm timeinfo = {0};       // Estructura tm inicializada a cero
    timeinfo.tm_year = year - 1900; // tm_year espera años desde 1900
    timeinfo.tm_mon = month - 1;    // tm_mon es 0-11
    timeinfo.tm_mday = day;         // Día del mes 1-31
    timeinfo.tm_hour = hour;        // Hora 0-23
    timeinfo.tm_min = minute;       // Minutos 0-59
    timeinfo.tm_sec = second;       // Segundos 0-59
    timeinfo.tm_isdst = -1;         // Desconoce info de DST (dejar que mktime lo determine)

    time_t timestamp = mktime(&timeinfo); // Convierte tm a time_t (segundos desde 1970)
    return (uint32_t)timestamp;           // Retorna resultado como uint32_t
}

/**
 * @brief Configura la fecha/hora del RTC usando un timestamp Unix.
 * @param timestamp Timestamp Unix (segundos).
 * @return true si fue exitoso.
 */

bool MAX31328RTC::setUnixTimestamp(uint32_t timestamp) {
    time_t rawtime = timestamp;
    struct tm* timeinfo = localtime(&rawtime);
    
    return setDateTime( // Llama a setDateTime con los campos extraídos de timeinfo
        timeinfo->tm_year + 1900, 
        timeinfo->tm_mon + 1,
        timeinfo->tm_mday,
        timeinfo->tm_hour,
        timeinfo->tm_min,
        timeinfo->tm_sec
    );
}

/**
 * @brief Lee la temperatura interna del RTC (si el chip la provee).
 * @return Temperatura en °C o -999.0f en caso de error.
 */

float MAX31328RTC::getTemperature() {
    if (!initialized && !isPresent()) {
        return -999.0f;
    }
    
    // Leer registros de temperatura
    uint8_t tempMSB = readRegister(MAX31328_REG_TEMP_MSB); // Lee el byte MSB de temperatura
    uint8_t tempLSB = readRegister(MAX31328_REG_TEMP_LSB); // Lee el byte LSB de temperatura
    
    // Convertir a temperatura 
    int16_t tempRaw = (tempMSB << 8) | tempLSB; // Combina MSB y LSB en un entero de 16 bits
    tempRaw >>= 6;   // Ajusta los bits según formato del MAX31328 (temperatura en 10-bits con signo)
    
    float temperature = tempRaw * 0.25f;  // Cada increment representa 0.25°C → multiplicar por 0.25 
    
    return temperature; // Retorna temperatura en °C
}

/**
 * @brief Devuelve la fecha/hora formateada como string "YYYY-MM-DD HH:MM:SS".
 * @return String con fecha y hora o mensaje de error si falla lectura.
 */

String MAX31328RTC::getFormattedDateTime() {
    uint16_t year;
    uint8_t month, day, hour, minute, second; // Variables locales para recibir la fecha
    
    if (!getDateTime(year, month, day, hour, minute, second)) { // Si falla la lectura...
        return "Error leyendo RTC"; // Retorna mensaje de error
    }
    
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
            year, month, day, hour, minute, second);
    
    return String(buffer); // Convierte el buffer C a String de Arduino y lo retorna
}

/**
 * @brief Sincroniza el RTC con un servidor NTP usando la conexión WiFi del ESP.
 * @param ntpServer Host del servidor NTP (ej. "pool.ntp.org").
 * @param gmtOffset Offset horario en horas (ej. -5 para UTC-5).
 * @return true si la sincronización y actualización del RTC fue exitosa.
 */

bool MAX31328RTC::syncWithNTP(const char* ntpServer, int gmtOffset) {
    Serial.println("MAX31328: Sincronizando con NTP...");
    
    configTime(gmtOffset * 3600, 0, ntpServer); // Configura la librería de tiempo del sistema para NTP (offset en segundos)
    
    struct tm timeinfo; // Estructura para recibir la hora local del sistema
    if (!getLocalTime(&timeinfo, 10000)) {  // 10 segundos timeout -  10 segundos timeout para obtener tiempo NTP
        Serial.println("MAX31328: Error obteniendo tiempo NTP"); // Error si no llega respuesta NTP
        return false; // Retorna fallo si no hay respuesta
    }
    
    // Actualizar RTC con tiempo NTP
    bool result = setDateTime( // Actualiza RTC con la hora NTP obtenida
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );
    
    if (result) { // Si la actualización fue exitosa...
        Serial.println("MAX31328: Sincronizado con NTP exitosamente"); // Mensaje
    }
    
    return result; // Retorna true/false según resultado
}

/**
 * @brief Imprime información de depuración en Serial (estado, valores y registros).
 */

void MAX31328RTC::printDebugInfo() {
    Serial.println("=== MAX31328 DEBUG INFO ===");
    Serial.printf("Inicializado: %s\n", initialized ? "Sí" : "No");
    Serial.printf("Dirección I2C: 0x%02X\n", i2c_address);
    Serial.printf("Presente: %s\n", isPresent() ? "Sí" : "No");
    Serial.printf("Funcionando: %s\n", isRunning() ? "Sí" : "No");
    Serial.printf("Tiempo perdido: %s\n", hasLostTime() ? "Sí" : "No");
    
    if (isPresent()) { // Si el dispositivo responde...
        Serial.printf("Fecha/Hora: %s\n", getFormattedDateTime().c_str()); // Imprime fecha formateada
        Serial.printf("Unix timestamp: %u\n", getUnixTimestamp()); // Imprime timestamp
        Serial.printf("Temperatura: %.2f°C\n", getTemperature()); // Imprime temperatura interna
        
        printRegisters(); // Imprime registros para depuración
    }
    
    Serial.println("==========================");
}

/**
 * @brief Imprime registros principales del RTC (tiempo, control y status).
 */

void MAX31328RTC::printRegisters() {
    Serial.println("Registros principales:");
    
    // Registros de tiempo
    for (uint8_t i = 0; i <= 6; i++) { // Itera primeros 7 registros (segundos..año)
        uint8_t value = readRegister(i); // Lee registro i
        Serial.printf("  0x%02X: 0x%02X (%d BCD)\n", i, value, bcdToDec(value & 0x7F)); // Imprime valor y su conversión BCD
    }
    
    // Registros de control
    uint8_t control = readRegister(MAX31328_REG_CONTROL);   // Lee registro de control
    uint8_t status = readRegister(MAX31328_REG_STATUS);     // Lee registro de status
    Serial.printf("  Control (0x0E): 0x%02X\n", control);   // Imprime control
    Serial.printf("  Status (0x0F): 0x%02X\n", status);     // Imprime status
}

// ——— FUNCIONES AUXILIARES ———

/**
 * @brief Convierte un valor decimal (0..99) a BCD (binary coded decimal).
 * @param val Valor decimal.
 * @return Valor en formato BCD.
 */

uint8_t MAX31328RTC::decToBcd(uint8_t val) {
    return ((val / 10) << 4) + (val % 10);
}

/**
 * @brief Convierte de BCD a decimal.
 * @param val Valor en BCD.
 * @return Valor decimal.
 */

uint8_t MAX31328RTC::bcdToDec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

/**
 * @brief Escribe un único registro en el RTC vía I2C.
 * @param reg Dirección del registro.
 * @param value Valor a escribir.
 * @return true si la escritura fue exitosa.
 */

bool MAX31328RTC::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(i2c_address);
    Wire.write(reg);
    Wire.write(value);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) { // Si hubo error...
        Serial.printf("MAX31328: Error escribiendo reg 0x%02X: %d\n", reg, error); // Imprime detalle
        return false; // Retorna false en fallo
    }
    
    return true; // Retorna true si escribe sin errores
}

/**
 * @brief Lee un único registro del RTC vía I2C.
 * @param reg Dirección del registro.
 * @return Valor leído o 0xFF si hubo error.
 */

uint8_t MAX31328RTC::readRegister(uint8_t reg) {
    Wire.beginTransmission(i2c_address);
    Wire.write(reg);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) { // Si hay error...
        Serial.printf("MAX31328: Error en transmisión reg 0x%02X: %d\n", reg, error); // Mensaje de error
        return 0xFF; // Retorna 0xFF como indicador de fallo
    }
    
    Wire.requestFrom(i2c_address, (uint8_t)1); // Solicita 1 byte
    if (Wire.available()) { // Si hay datos disponibles...
        return Wire.read(); // Retorna el byte leído
    }
    
    Serial.printf("MAX31328: Sin datos disponibles reg 0x%02X\n", reg); // Mensaje si no hay datos
    return 0xFF; // Retorna 0xFF indicando fallo
}

/**
 * @brief Escribe múltiples registros consecutivos comenzando en startReg.
 * @param startReg Registro inicial.
 * @param buffer Puntero a datos a escribir.
 * @param length Número de bytes a escribir.
 * @return true si la escritura fue exitosa.
 */

bool MAX31328RTC::writeMultipleRegisters(uint8_t startReg, uint8_t* buffer, uint8_t length) {
    Wire.beginTransmission(i2c_address);
    Wire.write(startReg);
    
    for (uint8_t i = 0; i < length; i++) { // Escribe cada byte del buffer
        Wire.write(buffer[i]); // Envía byte
    }
    
    uint8_t error = Wire.endTransmission(); // Finaliza y obtiene código de error
    
    if (error != 0) { // En caso de error...
        Serial.printf("MAX31328: Error escribiendo múltiples registros desde 0x%02X: %d\n", 
                    startReg, error);
        return false;
    }
    
    return true; // Retorna true si OK
}

/**
 * @brief Lee múltiples registros consecutivos comenzando en startReg.
 * @param startReg Registro inicial.
 * @param buffer Puntero donde almacenar los bytes leídos.
 * @param length Número de bytes a leer.
 * @return true si la lectura fue exitosa.
 */

bool MAX31328RTC::readMultipleRegisters(uint8_t startReg, uint8_t* buffer, uint8_t length) {
    Wire.beginTransmission(i2c_address);
    Wire.write(startReg);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) { // Si hubo error, entonces...
        Serial.printf("MAX31328: Error en transmisión múltiple reg 0x%02X: %d\n", 
                    startReg, error);
        return false;
    }
    
    Wire.requestFrom(i2c_address, length); // Solicita 'length' bytes
    
    for (uint8_t i = 0; i < length; i++) { // Itera para leer cada byte
        if (Wire.available()) { // Si hay datos...
            buffer[i] = Wire.read(); // Almacena en buffer
        } else { // Si no hay suficientes datos...
            Serial.printf("MAX31328: Datos insuficientes en lectura múltiple\n"); // Mensaje
            return false; // Retorna false
        }
    }
    
    return true; // Retorna true si todo fue leído correctamente
}