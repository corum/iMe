// ATxmega32C4 http://www.atmel.com/Images/Atmel-8493-8-and-32-bit-AVR-XMEGA-Microcontrollers-ATxmega16C4-ATxmega32C4_Datasheet.pdf
// Header files
extern "C" {
	#include <asf.h>
}
#include <string.h>
#include <math.h>
#include "accelerometer.h"
#include "eeprom.h"
#include "fan.h"
#include "gcode.h"
#include "heater.h"
#include "led.h"
#include "motors.h"


// Definitions

// Firmware name and version
#ifndef FIRMWARE_NAME
	#define FIRMWARE_NAME "iMe"
#endif

#ifndef FIRMWARE_VERSION
	#define FIRMWARE_VERSION "1900000001"
#endif

// Unknown pins
#define UNKNOWN_PIN_1 IOPORT_CREATE_PIN(PORTA, 1) // Connected to transistors above the microcontroller
#define UNKNOWN_PIN_2 IOPORT_CREATE_PIN(PORTA, 5) // Connected to a resistor and capacitor in parallel to ground

// Configuration details
#define REQUEST_BUFFER_SIZE 10


// Request class
class Request {

	// Public
	public :
	
		// Constructor
		Request() {
		
			// Clear size
			size = 0;
		}
	
	// Size and buffer
	uint8_t size;
	char buffer[UDI_CDC_COMM_EP_SIZE + 1];
};


// Global variables
char serialNumber[USB_DEVICE_GET_SERIAL_NAME_LENGTH];
Request requests[REQUEST_BUFFER_SIZE];
uint16_t waitCounter;
Motors motors;


// Function prototypes

/*
Name: CDC RX notify callback
Purpose: Callback for when USB receives data
*/
void cdcRxNotifyCallback(uint8_t port);

/*
Name: floatToString
Purpose: Converts a float to a string
*/
void floatToString(float value, char *buffer);


// Main function
int main() {

	// Initialize interrupt controller
	pmic_init();
	
	// Initialize interrupt vectors
	irq_initialize_vectors();
	
	// Initialize system clock
	sysclk_init();
	
	// Initialize board
	board_init();
	
	// Initialize I/O ports
	ioport_init();
	
	// Initialize variables
	uint8_t currentProcessingRequest = 0;
	char responseBuffer[255];
	uint32_t currentLineNumber = 0;
	char numberBuffer[sizeof("4294967295")];
	Accelerometer accelerometer;
	Fan fan;
	Gcode gcode;
	Heater heater;
	Led led;
	motors.initialize();
	
	// Configure unknown pins to how the official firmware does
	ioport_set_pin_dir(UNKNOWN_PIN_1, IOPORT_DIR_OUTPUT);
	ioport_set_pin_level(UNKNOWN_PIN_1, IOPORT_PIN_LEVEL_LOW);
	ioport_set_pin_dir(UNKNOWN_PIN_2, IOPORT_DIR_INPUT);
	
	// Configure send wait interrupt on motors Vref timer overflow
	tc_set_overflow_interrupt_callback(&MOTORS_VREF_TIMER, []() -> void {
	
		// Check if time to send wait
		if(++waitCounter >= sysclk_get_cpu_hz() / MOTORS_VREF_TIMER_PERIOD) {
		
			// Reset wait counter
			waitCounter = 0;
			
			// Send wait
			udi_cdc_write_buf("wait\n", strlen("wait\n"));
		}
	});
	
	// Read serial from EEPROM
	nvm_eeprom_read_buffer(EEPROM_SERIAL_NUMBER_OFFSET, serialNumber, EEPROM_SERIAL_NUMBER_LENGTH);
	
	// Enable interrupts
	cpu_irq_enable();
	
	// Initialize USB
	udc_start();
	
	// Enable send wait interrupt
	waitCounter = 0;
	tc_set_overflow_interrupt_level(&MOTORS_VREF_TIMER, TC_INT_LVL_LO);
	
	// Main loop
	while(1) {
	
		// Delay to allow enough time for a response to be received
		delay_us(1);
		
		// Check if a current processing request is ready
		if(requests[currentProcessingRequest].size) {
		
			// Disable send wait interrupt
			tc_set_overflow_interrupt_level(&MOTORS_VREF_TIMER, TC_INT_LVL_OFF);
			
			// Parse command
			gcode.parseCommand(requests[currentProcessingRequest].buffer);
		
			// Clear request buffer size
			requests[currentProcessingRequest].size = 0;
			
			// Increment current processing request
			currentProcessingRequest = currentProcessingRequest == REQUEST_BUFFER_SIZE - 1 ? 0 : currentProcessingRequest + 1;
			
			// Clear response buffer
			*responseBuffer = 0;
			
			// Check if accelerometer is working
			if(accelerometer.isWorking) {
		
				// Check if command contains valid G-code
				if(!gcode.isEmpty()) {
			
					// Check if command has host command
					if(gcode.hasHostCommand()) {
				
						// Check if host command is to calibrate the accelerometer
						if(!strcmp(gcode.getHostCommand(), "Calibrate accelerometer")) {
						
							// Calibrate accelerometer
							accelerometer.calibrate();
							
							// Set response to confirmation
							strcpy(responseBuffer, "ok");
						}
						
						// Otherwise check if host command is to get accelerometer's values
						else if(!strcmp(gcode.getHostCommand(), "Accelerometer values")) {
					
							// Set response to value
							accelerometer.readAccelerationValues();
							strcpy(responseBuffer, "ok X:");
							ltoa(accelerometer.xAcceleration, numberBuffer, 10);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer, "mg Y:");
							ltoa(accelerometer.yAcceleration, numberBuffer, 10);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer, "mg Z:");
							ltoa(accelerometer.zAcceleration, numberBuffer, 10);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer, "mg");
						}
						
						// Otherwise check if host command is to get lock bits
						else if(!strcmp(gcode.getHostCommand(), "Lock bits")) {
						
							// Send lock bits
							strcpy(responseBuffer, "ok 0x");
							ltoa(NVM_LOCKBITS, numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
						}
						
						// Otherwise check if host command is to get fuse bytes
						else if(!strcmp(gcode.getHostCommand(), "Fuse bytes")) {
						
							// Send fuse bytes
							strcpy(responseBuffer, "ok 0:0x");
							ltoa(nvm_fuses_read(FUSEBYTE0), numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer," 1:0x");
							ltoa(nvm_fuses_read(FUSEBYTE1), numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer," 2:0x");
							ltoa(nvm_fuses_read(FUSEBYTE2), numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer," 3:0x");
							ltoa(nvm_fuses_read(FUSEBYTE3), numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer," 4:0x");
							ltoa(nvm_fuses_read(FUSEBYTE4), numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
							strcat(responseBuffer," 5:0x");
							ltoa(nvm_fuses_read(FUSEBYTE5), numberBuffer, 16);
							strcat(responseBuffer, numberBuffer);
						}
						
						// Otherwise check if host command is to get application
						else if(!strcmp(gcode.getHostCommand(), "Application")) {
						
							strcpy(responseBuffer, "ok");
							udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
						
							// Send application
							for(uint16_t i = APP_SECTION_START; i <= APP_SECTION_END; i++) {
								strcpy(responseBuffer, i == APP_SECTION_START ? "0x" : " 0x");
								ltoa(pgm_read_byte(i), numberBuffer, 16);
								strcat(responseBuffer, numberBuffer);
								if(i != APP_SECTION_END)
									udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
							}
						}
						
						// Otherwise check if host command is to get bootloader
						else if(!strcmp(gcode.getHostCommand(), "Bootloader")) {
						
							strcpy(responseBuffer, "ok");
							udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
						
							// Send bootloader
							for(uint16_t i = BOOT_SECTION_START; i <= BOOT_SECTION_END; i++) {
								strcpy(responseBuffer, i == BOOT_SECTION_START ? "0x" : " 0x");
								ltoa(pgm_read_byte(i), numberBuffer, 16);
								strcat(responseBuffer, numberBuffer);
								if(i != BOOT_SECTION_END)
									udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
							}
						}
						
						// Otherwise check if host command is to get program
						else if(!strcmp(gcode.getHostCommand(), "Program")) {
						
							strcpy(responseBuffer, "ok");
							udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
						
							// Send bootloader
							for(uint16_t i = PROGMEM_START; i <= PROGMEM_END; i++) {
								strcpy(responseBuffer, i == PROGMEM_START ? "0x" : " 0x");
								ltoa(pgm_read_byte(i), numberBuffer, 16);
								strcat(responseBuffer, numberBuffer);
								if(i != PROGMEM_END)
									udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
							}
						}
						
						// Otherwise check if host command is to get EEPROM
						else if(!strcmp(gcode.getHostCommand(), "EEPROM")) {
						
							strcpy(responseBuffer, "ok");
							udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
						
							// Send EEPROM
							for(uint16_t i = EEPROM_START; i <= EEPROM_END; i++) {
								strcpy(responseBuffer, i == EEPROM_START ? "0x" : " 0x");
								ltoa(nvm_eeprom_read_byte(i), numberBuffer, 16);
								strcat(responseBuffer, numberBuffer);
								if(i != EEPROM_END)
									udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
							}
						}
						
						// Otherwise check if host command is to get user signature
						else if(!strcmp(gcode.getHostCommand(), "User signature")) {
						
							strcpy(responseBuffer, "ok");
							udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
						
							// Send EEPROM
							for(uint16_t i = USER_SIGNATURES_START; i <= USER_SIGNATURES_END; i++) {
								strcpy(responseBuffer, i == USER_SIGNATURES_START ? "0x" : " 0x");
								ltoa(nvm_read_user_signature_row(i), numberBuffer, 16);
								strcat(responseBuffer, numberBuffer);
								if(i != USER_SIGNATURES_END)
									udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
							}
						}
						
						// Otherwise
						else
					
							// Set response to error
							strcpy(responseBuffer, "ok Error: Unknown host command");
					}
				
					// Otherwise
					else {
			
						// Check if command has an N parameter
						if(gcode.hasParameterN()) {
						
							// Check if command is a starting line number
							if(gcode.getParameterN() == 0 && gcode.getParameterM() == 110)
				
								// Reset current line number
								currentLineNumber = 0;
						
							// Check if command doesn't have a valid checksum
							if(!gcode.hasValidChecksum())
				
								// Set response to resend
								strcpy(responseBuffer, "rs");
							
							// Otherwise
							else {
					
								// Check if line number is correct
								if(gcode.getParameterN() == currentLineNumber)
					
									// Increment current line number
									currentLineNumber++;
						
								// Otherwise check if command has already been processed
								else if(gcode.getParameterN() < currentLineNumber)
						
									// Set response to skip
									strcpy(responseBuffer, "skip");
					
								// Otherwise
								else
					
									// Set response to resend
									strcpy(responseBuffer, "rs");
							}
						}
				
						// Check if response wasn't set
						if(!*responseBuffer) {
						
							// Initialize variables used by G-code commands
							uint32_t delayTime;
							int32_t speed;
			
							// Check if command has an M parameter
							if(gcode.hasParameterM()) {
				
								switch(gcode.getParameterM()) {
								
									// M17
									case 17:
									
										// Turn on motors
										motors.turnOn();
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
								
									// M18
									case 18:
									
										// Turn off motors
										motors.turnOff();
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// M104
									case 104 :
									
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// M105
									case 105:
						
										// Set response to temperature
										strcpy(responseBuffer, "ok\nT:");
										floatToString(static_cast<float>(heater.getTemperature()) * 2.60 / 2047, numberBuffer);
										strcat(responseBuffer, numberBuffer);
									break;
									
									// M106
									case 106:
									
										// Check if speed is valid
										speed = gcode.hasParameterS() ? gcode.getParameterS() : 0;
										if(speed >= 0 && speed <= 255) {
								
											// Set fan's speed
											fan.setSpeed(speed);
										
											// Set response to confirmation
											strcpy(responseBuffer, "ok");
										}
									break;
									
									// M107
									case 107:
									
										// Turn off fan
										fan.turnOff();
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// M109
									case 109:
									
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// M114
									case 114:
									
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
										
										// Check if motors coordinates are valid
										if(!isnan(motors.currentX)) {
										
											// Append motors current X to response
											strcat(responseBuffer, " X:");
											floatToString(motors.currentX, numberBuffer);
											strcat(responseBuffer, numberBuffer);
										
											// Append motors current Y to response
											strcat(responseBuffer, " Y:");
											floatToString(motors.currentY, numberBuffer);
											strcat(responseBuffer, numberBuffer);
										
											// Append motors current E to response
											strcat(responseBuffer, " E:");
											floatToString(motors.currentE, numberBuffer);
											strcat(responseBuffer, numberBuffer);
										}
										
										// Append motors current Z to response
										strcat(responseBuffer, " Z:");
										floatToString(motors.currentZ, numberBuffer);
										strcat(responseBuffer, numberBuffer);
									break;
							
									// M115
									case 115:
							
										// Check if command is to reset
										if(gcode.getParameterS() == 628)
							
											// Perform software reset
											reset_do_soft_reset();
							
										// Otherwise
										else {
							
											// Put device details into response
											strcpy(responseBuffer, "ok REPRAP_PROTOCOL:0 FIRMWARE_NAME:" FIRMWARE_NAME " FIRMWARE_VERSION:" FIRMWARE_VERSION " MACHINE_TYPE:The_Micro X-SERIAL_NUMBER:");
											strncat(responseBuffer, serialNumber, EEPROM_SERIAL_NUMBER_LENGTH);
										}
									break;
									
									// M117
									case 117:
									
										// Set response to valid Z
										strcpy(responseBuffer, "ok ZV:");
										strcat(responseBuffer, nvm_eeprom_read_byte(EEPROM_SAVED_Z_STATE_OFFSET) ? "1" : "0");
									break;
									
									// M420
									case 420:
									
										// Check if duty cycle is provided
										if(gcode.hasParameterT()) {
										
											// Check if brightness is valid
											uint8_t brightness = gcode.getParameterT();
											
											if(brightness <= 100) {
											
												// Set LED's brightness
												led.setBrightness(brightness);
												
												// Set response to confirmation
												strcpy(responseBuffer, "ok");
											}
										}
									break;
								
									// M618
									case 618:
								
										// Check if EEPROM offset, length, and value are provided
										if(gcode.hasParameterS() && gcode.hasParameterT() && gcode.hasParameterP()) {
									
											// Check if offset and length are valid
											int32_t offset = gcode.getParameterS();
											uint8_t length = gcode.getParameterT();
										
											if(offset >= 0 && length && length <= 4 && offset + length < EEPROM_SIZE) {
										
												// Get value
												int32_t value = gcode.getParameterP();
										
												// Write value to EEPROM
												nvm_eeprom_erase_and_write_buffer(offset, &value, length);
											
												// Set response to confirmation
												strcpy(responseBuffer, "ok PT:");
												ultoa(offset, numberBuffer, 10);
												strcat(responseBuffer, numberBuffer);
											}
										}
									break;
								
									// M619
									case 619:
								
										// Check if EEPROM offset and length are provided
										if(gcode.hasParameterS() && gcode.hasParameterT()) {
									
											// Check if offset and length are valid
											int32_t offset = gcode.getParameterS();
											uint8_t length = gcode.getParameterT();
										
											if(offset >= 0 && length && length <= 4 && offset + length < EEPROM_SIZE) {
										
												// Get value from EEPROM
												uint32_t value = 0;
												nvm_eeprom_read_buffer(offset, &value, length);
											
												// Set response to value
												strcpy(responseBuffer, "ok PT:");
												ultoa(offset, numberBuffer, 10);
												strcat(responseBuffer, numberBuffer);
												strcat(responseBuffer, " DT:");
												ultoa(value, numberBuffer, 10);
												strcat(responseBuffer, numberBuffer);
											}
										}
									break;
									
									// M0, M21 ,M84, or M110
									case 0:
									case 21:
									case 84:
									case 110:
							
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
								}
							}
						
							// Otherwise check if command has a G parameter
							else if(gcode.hasParameterG()) {
				
								switch(gcode.getParameterG()) {
					
									// G0 or G1
									case 0:
									case 1:
									
										// Check if command contains a valid parameter
										if(gcode.hasParameterX() || gcode.hasParameterY() || gcode.hasParameterZ() || gcode.hasParameterE() || gcode.hasParameterF()) {
										
											// Move
											motors.move(gcode);
							
											// Set response to confirmation
											strcpy(responseBuffer, "ok");
										}
									break;
							
									// G4
									case 4:
							
										// Delay specified time
										if((delayTime = gcode.getParameterP() + gcode.getParameterS() * 1000))
											delay_ms(delayTime);
								
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// G28
									case 28:
									
										// Home motors
										motors.goHome();
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// G30
									case 30:
									
									break;
									
									// G32
									case 32:
									
									break;
									
									// G33
									case 33:
									
										// Set Z to zero
										motors.setZToZero();
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// G90
									case 90:
									
										// Set mode to absolute
										motors.mode = ABSOLUTE;
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// G91
									case 91:
									
										// Set mode to relative
										motors.mode = RELATIVE;
										
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
									
									// G92
									case 92:
									
										// Set motors current E
										motors.currentX = gcode.hasParameterE() ? gcode.getParameterE() : 0;
							
										// Set response to confirmation
										strcpy(responseBuffer, "ok");
									break;
								}
							}
							
							// Otherwise check if command has parameter T
							else if(gcode.hasParameterT())
							
								// Set response to confirmation
								strcpy(responseBuffer, "ok");
						}
					
						// Check if command has an N parameter and it was processed
						if(gcode.hasParameterN() && (!strncmp(responseBuffer, "ok", 2) || !strncmp(responseBuffer, "rs", 2) || !strncmp(responseBuffer, "skip", 4))) {
			
							// Append line number to response
							uint8_t endOfResponse = responseBuffer[0] == 's' ? 4 : 2;
							uint32_t value = responseBuffer[0] == 'r' ? currentLineNumber : gcode.getParameterN();
						
							ultoa(value, numberBuffer, 10);
							memmove(&responseBuffer[endOfResponse + 1 + strlen(numberBuffer)], &responseBuffer[endOfResponse], strlen(responseBuffer) - 1);
							responseBuffer[endOfResponse] = ' ';
							memcpy(&responseBuffer[endOfResponse + 1], numberBuffer, strlen(numberBuffer));
						}
					}
				}
			}
			
			// Otherwise
			else
			
				// Set response to error
				strcpy(responseBuffer, "ok Error: accelerometer isn't working");
			
			// Check if response wasn't set
			if(!*responseBuffer)
			
				// Set response to error
				strcpy(responseBuffer, "ok Error: Unknown G-code command\n");
			
			// Otherwise
			else
			
				// Append newline to response
				strcat(responseBuffer, "\n");
			
			// Send response
			udi_cdc_write_buf(responseBuffer, strlen(responseBuffer));
			
			// Enable send wait interrupt
			waitCounter = 0;
			tc_set_overflow_interrupt_level(&MOTORS_VREF_TIMER, TC_INT_LVL_LO);
		}
	}
	
	// Return 0
	return 0;
}


// Supporting function implementation
void cdcRxNotifyCallback(uint8_t port) {

	// Initialize variables
	static uint8_t currentReceivingRequest = 0;

	// Check if currently receiving request is empty
	if(!requests[currentReceivingRequest].size) {
	
		// Get request
		requests[currentReceivingRequest].size = udi_cdc_multi_get_nb_received_data(port);
		udi_cdc_multi_read_buf(port, requests[currentReceivingRequest].buffer, requests[currentReceivingRequest].size);
		requests[currentReceivingRequest].buffer[requests[currentReceivingRequest].size] = 0;
		
		// Check if request isn't empty
		if(requests[currentReceivingRequest].size) {
		
			// Check if request contains an emergency stop
			char *emergencyStopOffset = strstr(requests[currentReceivingRequest].buffer, "M0");
			if(emergencyStopOffset) {
			
				// Check if emergency stop isn't after the start of a host command, comment, or checksum
				char *characterOffset = strpbrk(requests[currentReceivingRequest].buffer, "@*;");
				if(!characterOffset || emergencyStopOffset < characterOffset)
			
					// Stop motors
					motors.emergencyStop();
			}
			
			// Increment current receiving request
			currentReceivingRequest = currentReceivingRequest == REQUEST_BUFFER_SIZE - 1 ? 0 : currentReceivingRequest + 1;
		}
	}
}

void floatToString(float value, char *buffer) {

	// Convert value to string
	dtostrf(value, 9, 4, buffer);
	
	// Remove leading whitespace
	char *firstValidCharacter = buffer;
	while(*firstValidCharacter++ == ' ');
	
	// Prepend a zero if string doesn't contain one
	if(*(--firstValidCharacter) == '.')
		*(--firstValidCharacter) = '0';
	
	// Move string to the start of the buffer
	memmove(buffer, firstValidCharacter, strlen(firstValidCharacter) + 1);
}
