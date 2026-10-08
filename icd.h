#ifndef ICD_H
#define ICD_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 16 // Taille max du payload

// Flags de commands
typedef enum {
    CMD_INIT_RESET = 0x00,
    CMD_MESSAGE    = 0x01,
    CMD_GET_KEY    = 0x03, //demander la clé de chiffrement
    CMD_UPDT_KEY   = 0x04,
    CMD_ACK        = 0x05, //retour du n+1
    CMD_ERR        = 0x02
} CommandType;

// status de la carte
typedef enum {
    STATUS_UNKNOWN = 0x00,
    STATUS_MASTER  = 0x01,
    STATUS_SLAVE   = 0x02
} NodeStatus;

// Header
typedef struct __attribute__((packed)){
    uint32_t id_src;
    uint8_t length;           // combien d'octets dans le payload ?
    uint8_t command_type;     
} FrameHeader;
//pour garantir que la taille de la structure en RAM est exactement la meme que la structure envoyée

//erreur : 

// Payload
typedef struct {
    uint8_t data[MAX_PAYLOAD_SIZE]; // Réserve toujours 16 octets en mémoire
}Payload;

// Structure Globale du Message
typedef struct {
    FrameHeader header;
    Payload     payload;
    uint16_t    crc16;        
}GlobalMessage;

#endif // ICD_H