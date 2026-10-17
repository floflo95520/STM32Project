/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "icd.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    NODE_ROLE_UNASSIGNED = 0,
    NODE_ROLE_MASTER,
    NODE_ROLE_SLAVE,
    NODE_ROLE_LAST_SLAVE
} NodeRole_t;

typedef enum {
    LED_MODE_WAIT_INIT, // Clignote lentement
    LED_MODE_SYNC,      // Allumee
    LED_MODE_ERROR      // Clignote vite
} LedState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
// Etat du noeud par défaut, on clignote en attendant, la clé 0 etc..
NodeRole_t current_role = NODE_ROLE_UNASSIGNED;
LedState_t current_led_mode = LED_MODE_WAIT_INIT;
uint32_t   current_key = 0;
uint32_t   my_id = HAL_GetUIDw0();; // ID de la carte, à changer pour prendre le vrai ID

// Compteur du bouton
volatile uint8_t  button_press_count = 0;
volatile uint32_t last_button_time = 0;

// Octets recus en interruption 
uint8_t rx1_byte;
uint8_t rx2_byte;

// Buffers separes pour les deux UART
static uint8_t rx1_buffer[sizeof(ProtocolMessage_t)];
static uint8_t rx1_index = 0;
static uint8_t rx1_receiving = 0;

static uint8_t rx2_buffer[sizeof(ProtocolMessage_t)];
static uint8_t rx2_index = 0;
static uint8_t rx2_receiving = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
uint16_t compute_crc16(const uint8_t *data, uint16_t length);
void xor_cipher(uint8_t *payload, uint8_t len, uint32_t key);
void process_received_frame(ProtocolMessage_t *msg, UART_HandleTypeDef *source_huart);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// fonction crc16 générée à l'aide de l'ia, fonction générique de checksum
uint16_t compute_crc16(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    }
    return crc;
}

// généré à l'aide de l'ia, pour cryptage
void xor_cipher(uint8_t *payload, uint8_t len, uint32_t key) {
    uint8_t key_bytes[4];
    key_bytes[0] = (uint8_t)(key & 0xFF);
    key_bytes[1] = (uint8_t)((key >> 8) & 0xFF);
    key_bytes[2] = (uint8_t)((key >> 16) & 0xFF);
    key_bytes[3] = (uint8_t)((key >> 24) & 0xFF);

    for (uint8_t i = 0; i < len; i++) {
        payload[i] ^= key_bytes[i % 4];
    }
}

//Fonction pour recevoir les trames reçu 
void process_received_frame(ProtocolMessage_t *msg, UART_HandleTypeDef *source_huart) {
    uint16_t expected_len = sizeof(ProtocolMessage_t) - sizeof(uint16_t);
    uint16_t local_crc = compute_crc16((uint8_t*)msg, expected_len);

    // Verifie le CRC des messages UART1 -> vérifier l'intégrité du message
    if (source_huart == &huart1) {
        if (local_crc != msg->crc) {
            current_led_mode = LED_MODE_ERROR; // Erreur CRC, clignotement rapide
            return;
        }
    }

    switch (msg->header.command_type) {
        case CMD_INIT:
            if (msg->header.id_src == DEFAULT_ID) {
                // cas ou on est master, car on reçoit un message avec src_id 0x00
                current_role = NODE_ROLE_MASTER;
                current_key = HAL_GetTick() ^ 0xA5A5A5A5;
                if (current_key == 0) current_key = 0x12345678;
                current_led_mode = LED_MODE_SYNC;

                // Propagation de l'INIT vers la carte suivante
                ProtocolMessage_t out_msg;
                memset(&out_msg, 0, sizeof(out_msg));
                out_msg.header.start_byte = ICD_START_BYTE;
                out_msg.header.command_type = CMD_INIT;
                out_msg.header.id_src = my_id;
                out_msg.header.length = 4;
                memcpy(out_msg.payload, &current_key, 4);
                out_msg.crc = compute_crc16((uint8_t*)&out_msg, expected_len);
                HAL_UART_Transmit(&huart1, (uint8_t*)&out_msg, sizeof(out_msg), 100);
            } 
            else {
                // --- ESCLAVE (Réception de l'INIT) ---
                // On suppose qu'on est le DERNIER par défaut (car c'est plus simple)
                current_role = NODE_ROLE_LAST_SLAVE; 
                memcpy(&current_key, msg->payload, 4);
                current_led_mode = LED_MODE_SYNC;

                // On répond un ACK au noeud précédent pour confirmer la synchro
                ProtocolMessage_t ack_msg;
                memset(&ack_msg, 0, sizeof(ack_msg));
                ack_msg.header.start_byte = ICD_START_BYTE;
                ack_msg.header.command_type = CMD_ACK;
                ack_msg.header.id_src = my_id;
                ack_msg.crc = compute_crc16((uint8_t*)&ack_msg, expected_len);
                HAL_UART_Transmit(source_huart, (uint8_t*)&ack_msg, sizeof(ack_msg), 100);

                // On propage l'INIT vers l'avant (pour interroger s'il y a un noeud suivant)
                msg->header.id_src = my_id;
                msg->crc = compute_crc16((uint8_t*)msg, expected_len);
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, sizeof(ProtocolMessage_t), 100);
            }
            break;
            
        case CMD_MSG: // ici on vérifie si on est la derniere carte ou pas, en envoyant un message au noeud suivant
                    // si retour ACK avec id null => pc donc on est le dernier noeud, sinon il y a encore des cartes derriere
            // MAÎTRE : Reçoit le texte en clair du PC, chiffre et relaye
            if (source_huart == &huart2 && current_role == NODE_ROLE_MASTER) {
                xor_cipher(msg->payload, msg->header.length, current_key);
                msg->header.id_src = my_id;
                msg->crc = compute_crc16((uint8_t*)msg, expected_len);
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, sizeof(ProtocolMessage_t), 100);
            }
            // DERNIER ESCLAVE : Déchiffre et envoie vers le PC OUT
            else if (source_huart == &huart1 && current_role == NODE_ROLE_LAST_SLAVE) {
                xor_cipher(msg->payload, msg->header.length, current_key); // Déchiffrement
                
                char log_buffer[64];
                snprintf(log_buffer, sizeof(log_buffer), "\r\n[PC OUT RECU] : %.*s\r\n", 
                         msg->header.length, msg->payload);
                HAL_UART_Transmit(&huart2, (uint8_t*)log_buffer, strlen(log_buffer), 100);
            }
            // ESCLAVE INTERMÉDIAIRE : Relaye le message chiffré sans le déchiffrer
            else if (source_huart == &huart1 && current_role == NODE_ROLE_SLAVE) {
                msg->header.id_src = my_id;
                // /!\ On ne déchiffre PAS le payload
                msg->crc = compute_crc16((uint8_t*)msg, expected_len);
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, sizeof(ProtocolMessage_t), 100);
            }

            // Quel que soit le type d'esclave, on accuse réception au noeud N-1
            if (source_huart == &huart1) {
                ProtocolMessage_t ack_msg;
                memset(&ack_msg, 0, sizeof(ack_msg));
                ack_msg.header.start_byte = ICD_START_BYTE;
                ack_msg.header.command_type = CMD_ACK;
                ack_msg.header.id_src = my_id;
                ack_msg.crc = compute_crc16((uint8_t*)&ack_msg, expected_len);
                HAL_UART_Transmit(&huart1, (uint8_t*)&ack_msg, sizeof(ack_msg), 100);
            }
            break;

            case CMD_ACK:
            // L'interrogation : On a reçu une réponse du noeud suivant !
            if (current_role == NODE_ROLE_LAST_SLAVE) {
                if (msg->header.id_src != DEFAULT_ID) {
                    // Une vraie carte a répondu avec son ID : on n'est donc PAS le dernier !
                    current_role = NODE_ROLE_SLAVE; 
                }
            }
            break;

        default:
            break;
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  // Demarre la reception UART
  HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
  HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_led_toggle = 0;

  while (1)
  {
    uint32_t now = HAL_GetTick();

    // Reset apres 3 appuis (bouton bleu)
    if (button_press_count > 0 && (now - last_button_time > 1500)) {
        if (button_press_count >= 3) {
            current_led_mode = LED_MODE_WAIT_INIT;
            current_key = 0;
            current_role = NODE_ROLE_UNASSIGNED;
        }
        button_press_count = 0;
    }

    // Gestion de la LED LD2
    switch (current_led_mode) {
        case LED_MODE_WAIT_INIT: // Clignotement lent
            if (now - last_led_toggle >= 1000) {
                HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
                last_led_toggle = now;
            }
            break;

        case LED_MODE_SYNC: // LED allumee
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
            break;

        case LED_MODE_ERROR: // Clignotement rapide
            if (now - last_led_toggle >= 100) {
                HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
                last_led_toggle = now;
            }
            break;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /* PC13 (User button) */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* PA5 (LD2 LED) */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_13) {
        uint32_t now = HAL_GetTick();
        if (now - last_button_time > 200) {
            button_press_count++;
            last_button_time = now;
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart2) {
        if (!rx2_receiving) {
            if (rx2_byte == ICD_START_BYTE) {
                rx2_receiving = 1;
                rx2_index = 0;
                rx2_buffer[rx2_index++] = rx2_byte;
            }
        } else {
            rx2_buffer[rx2_index++] = rx2_byte;
            if (rx2_index >= sizeof(ProtocolMessage_t)) {
                rx2_receiving = 0;
                process_received_frame((ProtocolMessage_t*)rx2_buffer, &huart2);
            }
        }
        HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
    } 
    else if (huart == &huart1) {
        if (!rx1_receiving) {
            if (rx1_byte == ICD_START_BYTE) {
                rx1_receiving = 1;
                rx1_index = 0;
                rx1_buffer[rx1_index++] = rx1_byte;
            }
        } else {
            rx1_buffer[rx1_index++] = rx1_byte;
            if (rx1_index >= sizeof(ProtocolMessage_t)) {
                rx1_receiving = 0;
                process_received_frame((ProtocolMessage_t*)rx1_buffer, &huart1);
            }
        }
        HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart2) {
        __HAL_UART_CLEAR_OREFLAG(&huart2);
        HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
    } else if (huart == &huart1) {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
    }
}
/* USER CODE END 4 */

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */