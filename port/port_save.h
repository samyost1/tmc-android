#ifndef PORT_SAVE_H
#define PORT_SAVE_H

#include <stddef.h>
#include <stdint.h>

#define PORT_SAVE_EEPROM_BYTES 8192u

/*
 * Copies EEPROM in mGBA's direct RA-visible/on-disk byte order. The port keeps
 * each 8-byte block reversed in game-RAM order internally.
 */
void Port_Save_ReadEepromRaSnapshot(uint8_t out[PORT_SAVE_EEPROM_BYTES]);

#endif
