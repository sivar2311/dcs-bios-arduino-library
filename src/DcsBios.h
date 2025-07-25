#ifndef __DCSBIOS_H
#define __DCSBIOS_H

#ifndef NULL
#define NULL 0
#endif

#include <stdint.h>

#ifdef DCSBIOS_FOR_STM32
#include <itoa.h>
#endif

#include "internal/ExportStreamListener.h"
#include "internal/PollingInput.h"
#include "internal/Protocol.h"
#include "internal/Protocol.cpp.inc" // Needs to be a .cpp.inc to allow DCSBIOS_INCOMING_DATA_BUFFER_SIZE
#include "internal/Addresses.h"


#ifndef USART0_RX_vect
#define USART0_RX_vect USART_RX_vect
#define USART0_TX_vect USART_TX_vect
#define USART0_UDRE_vect USART_UDRE_vect
#endif

#ifndef PRR0
#define PRR0 PRR
#endif

namespace DcsBios {
	const unsigned char PIN_NC = 0xFF;
}

/*
The following is an ugly hack to work with the Arduino IDE's build system.
The DCS-BIOS Arduino Library is configured with #defines such as DCSBIOS_RS485_MASTER or DCSBIOS_RS485_SLAVE <address>.
To make sure these defines are visible when compiling the code, we can't put it into a separate translation unit.

Normally, those #defines would go in a separate "config.h" or you would use compiler flags. But since Arduino libraries
do not come with their own build system, we are just putting everything into the header file.
*/
#ifdef DCSBIOS_RS485_MASTER
	#include "internal/DcsBiosNgRS485Master.h"
	#include "internal/DcsBiosNgRS485Master.cpp.inc"
#endif
#ifdef DCSBIOS_RS485_SLAVE
	#if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
		#include "internal/UART.Mod/DcsBiosNgRS485Slave.h"
		#include "internal/UART.Mod/DcsBiosNgRS485Slave.cpp.inc"
	#else
		#include "internal/DcsBiosNgRS485Slave.h"
		#include "internal/DcsBiosNgRS485Slave.cpp.inc"
	#endif
#endif
#ifdef DCSBIOS_ESP32_WIFI
	#include "internal/ESP32WiFi/DcsBiosESP32WiFiSlave.h"
	#include "internal/ESP32WiFi/DcsBiosESP32WiFiSlave.cpp.inc"
#endif
#ifdef DCSBIOS_IRQ_SERIAL

	namespace DcsBios {
		ProtocolParser parser;

		ISR(USART0_RX_vect) {
			volatile uint8_t c = UDR0;
			parser.processCharISR(c);
		}
		
		void setup() {
			PRR0 &= ~(1<<PRUSART0);
			UBRR0H = 0;
			UBRR0L = 3; // 250000 bps
			UCSR0A = 0;
			UCSR0C = (1<<UCSZ00) | (1<<UCSZ01);
			
			UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0);
		}
		
		void loop() {
			PollingInput::pollInputs();
			ExportStreamListener::loopAll();
		}

		void resetAllStates() {
			PollingInput::resetAllStates();
		}

		static void usart_tx(const char* str) {
			const char* c = str;
			while (*c) {
				while(!(UCSR0A & (1<<UDRE0))); // wait until TX buffer is empty
				UDR0 = *c++; // write byte to TX buffer
			}
		}
		
		bool tryToSendDcsBiosMessage(const char* msg, const char* arg) {
			DcsBios::usart_tx(msg);
			DcsBios::usart_tx(" ");
			DcsBios::usart_tx(arg);
			DcsBios::usart_tx("\n");
			DcsBios::PollingInput::setMessageSentOrQueued();
			return true;
		}
	}
#endif
#ifdef DCSBIOS_DEFAULT_SERIAL
	namespace DcsBios {
		ProtocolParser parser;
		void setup() {
			Serial.begin(250000);
		}
		void loop() {
			while (Serial.available()) {
				parser.processChar(Serial.read());
			}
			PollingInput::pollInputs();
			ExportStreamListener::loopAll();			
		}
		bool tryToSendDcsBiosMessage(const char* msg, const char* arg) {
			Serial.write(msg); Serial.write(' '); Serial.write(arg); Serial.write('\n');
			DcsBios::PollingInput::setMessageSentOrQueued();
			return true;
		}
		void resetAllStates() {
			PollingInput::resetAllStates();
		}
	}
#endif

#ifdef DCSBIOS_ESP32_UDP
#include <WiFi.h>
#include <WiFiUdp.h>
namespace DcsBios {

	ProtocolParser parser;
	WiFiUDP udp;

	const IPAddress invalid(0, 0, 0, 0);
	const IPAddress dcsBiosUDPBroadcastAddress(239, 255, 50, 10);
	const uint16_t  dcsBiosUDPBroardcastPort = 5010;

	IPAddress       dcsBiosUDPRemoteAddress = invalid;
	const uint16_t  dcsBiosUDPRemotePort = 7778;

	unsigned long lastDcsBiosMessageReceived = 1000;

	#ifndef DCSBIOS_LOG_OUTPUT
	#define DCSBIOS_LOG(args...)
	#else
	#define DCSBIOS_LOG(args...) DCSBIOS_LOG_OUTPUT.printf(args)
	#endif

	void setup() {
		udp.beginMulticast(dcsBiosUDPBroadcastAddress, dcsBiosUDPBroardcastPort);
	}

	void loop() {
		if (WiFi.status() != WL_CONNECTED) return;

		unsigned long now = millis();
		if (lastDcsBiosMessageReceived && now - lastDcsBiosMessageReceived >= 1000) {
			dcsBiosUDPRemoteAddress = invalid;
			lastDcsBiosMessageReceived = 0;
			DCSBIOS_LOG("Waiting for DCS-BIOS UDP packets...\r\n");
		}

		while (udp.parsePacket()) {
			if (udp.remoteIP() != dcsBiosUDPRemoteAddress) {
				dcsBiosUDPRemoteAddress = udp.remoteIP();
				DCSBIOS_LOG("Receiving DCS-BIOS packets from: %s\n", dcsBiosUDPRemoteAddress.toString().c_str());
			}

			lastDcsBiosMessageReceived = now;

			while (udp.available()) {
				char c = udp.read();
				DCSBIOS_LOG("%c", c);
				parser.processChar(c);
			}
			DCSBIOS_LOG("\r\n");
		}
		PollingInput::pollInputs();
		ExportStreamListener::loopAll();
	}

	bool tryToSendDcsBiosMessage(const char* msg, const char* arg) {
		if (dcsBiosUDPRemoteAddress == invalid) {
			return false;
		}

		udp.beginPacket(dcsBiosUDPRemoteAddress, dcsBiosUDPRemotePort);
		udp.printf("%s %s\n", msg, arg);
		udp.endPacket();

		DcsBios::PollingInput::setMessageSentOrQueued();
		return true;
	}
}
#endif


#include "internal/Buttons.h"
#include "internal/Switches.h"
#include "internal/SyncingSwitches.h"
#include "internal/Encoders.h"
#include "internal/Potentiometers.h"
#include "internal/RotarySyncingPotentiometer.h"
#include "internal/Leds.h"
#ifndef DCSBIOS_DISABLE_SERVO
#include "internal/Servos.h"
#endif
#include "internal/Dimmer.h"
#include "internal/BcdWheels.h"
#include "internal/AnalogMultiPos.h"
#include "internal/RotarySwitch.h"
#if defined(USE_MATRIX_SWITCHES) || defined(DCSBIOS_USE_MATRIX_SWITCHES)
#include "internal/MatrixSwitches.h"
#endif
#include "internal/DualModeButton.h"

namespace DcsBios {
	template<unsigned int first, unsigned int second>
	unsigned int piecewiseMap(unsigned int newValue) {
		return 0;
	}

	template<unsigned int from1, unsigned int to1, unsigned int from2, unsigned int to2, unsigned int... rest>
	unsigned int piecewiseMap(unsigned int newValue) {
		if (newValue < from2) {
			return map(newValue, from1, from2, to1, to2);
		} else {
			return piecewiseMap<from2, to2, rest...>(newValue);
		}
	}
}

#ifndef DCSBIOS_RS485_MASTER
namespace DcsBios {	
	inline bool sendDcsBiosMessage(const char* msg, const char* arg) {
		while(!tryToSendDcsBiosMessage(msg, arg));
		return true;
	}
}

// for backwards compatibility, can be removed when we have a proper place to document this interface:
inline bool sendDcsBiosMessage(const char* msg, const char* arg) {
	while(!DcsBios::tryToSendDcsBiosMessage(msg, arg));
	return true;
}
#endif

#endif // include guard
