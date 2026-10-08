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
    LED_MODE_WAIT_INIT, // Clignote a 0.5 Hz
    LED_MODE_SYNC,      // Fixe
    LED_MODE_ERROR      // Clignote a 5 Hz
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
// Etat local
NodeRole_t current_role = NODE_ROLE_UNASSIGNED;
LedState_t current_led_mode = LED_MODE_WAIT_INIT;
uint32_t   current_key = 0;
uint32_t   my_id = 0xCAFEBABE; // ID unique propre a cette carte

// Compteur d'appuis bouton
volatile uint8_t  button_press_count = 0;
volatile uint32_t last_button_time = 0;

// Buffers de trame UART (1 octet a la fois en IT)
uint8_t rx1_byte;
uint8_t rx2_byte;

// Buffer temporaire pour reconstruire une trame
static uint8_t rx_buffer[sizeof(ProtocolMessage_t)];
static uint8_t rx_index = 0;
static uint8_t receiving_frame = 0;
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
  // Demarrage ecoute UART en interruption
  HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
  HAL_UART_Receive_IT(&huart1, &rx1_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_led_toggle = 0;

  while (1)
  {
    uint32_t now = HAL_GetTick();

    // 1. Detection des 3 appuis bouton espaces
    if (button_press_count > 0 && (now - last_button_time > 1500)) {
        if (button_press_count >= 3) {
            current_led_mode = LED_MODE_WAIT_INIT;
            current_key = 0;
            current_role = NODE_ROLE_UNASSIGNED;
        }
        button_press_count = 0;
    }

    // 2. Gestion frequences LED LD2 (PA5)
    switch (current_led_mode) {
        case LED_MODE_WAIT_INIT: // 0.5 Hz = 1000ms ON / 1000ms OFF
            if (now - last_led_toggle >= 1000) {
                HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
                last_led_toggle = now;
            }
            break;

        case LED_MODE_SYNC: // Fixe allumee
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
            break;

        case LED_MODE_ERROR: // 5 Hz = 100ms ON / 100ms OFF
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

void process_received_frame(ProtocolMessage_t *msg, UART_HandleTypeDef *source_huart) {
    uint16_t expected_len = sizeof(ProtocolMessage_t) - sizeof(uint16_t);
    uint16_t local_crc = compute_crc16((uint8_t*)msg, expected_len);

    // Si le message vient d'une autre carte (USART1), on contrôle strictement le CRC
    if (source_huart == &huart1) {
        if (local_crc != msg->crc) {
            current_led_mode = LED_MODE_ERROR; // Clignote à 5 Hz
            return;
        }
    }

    switch (msg->header.command_type) {
        case CMD_INIT:
            if (msg->header.id_src == DEFAULT_ID) {
                // Initialisation commandée par le PC
                current_role = NODE_ROLE_MASTER;
                current_key = HAL_GetTick() ^ 0xA5A5A5A5;
                if (current_key == 0) current_key = 0x12345678;
                
                current_led_mode = LED_MODE_SYNC; // LED FIXE !
            }
            break;
            
        case CMD_MSG:
            // 1. Message venant du PC (USART2) -> On chiffre et on envoie sur la ligne inter-cartes (USART1)
            if (source_huart == &huart2) {
                // Chiffrement du texte dans le payload
                xor_cipher(msg->payload, msg->header.length, current_key);
                
                // Préparation du message à transmettre
                msg->header.id_src = my_id;
                uint16_t out_len = sizeof(ProtocolMessage_t) - sizeof(uint16_t);
                msg->crc = compute_crc16((uint8_t*)msg, out_len);
                
                // Émission sur USART1 (PA9)
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, sizeof(ProtocolMessage_t), 100);
            }
            // 2. Message reçu depuis l'autre carte (USART1 / PA10) -> Déchiffrement et restitution
            else if (source_huart == &huart1) {
                // Déchiffrement avec la même clé
                xor_cipher(msg->payload, msg->header.length, current_key);

                // Envoi du texte clair vers le PC (USART2 / USB) pour affichage terminal
                char log_buffer[64];
                snprintf(log_buffer, sizeof(log_buffer), "\r\n[RECU EN CLAIR] : %.*s\r\n", 
                         msg->header.length, msg->payload);
                HAL_UART_Transmit(&huart2, (uint8_t*)log_buffer, strlen(log_buffer), 100);
            }
            break;

        default:
            break;
    }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    uint8_t byte = (huart == &huart2) ? rx2_byte : rx1_byte;

    if (!receiving_frame) {
        if (byte == ICD_START_BYTE) {
            receiving_frame = 1;
            rx_index = 0;
            rx_buffer[rx_index++] = byte;
        }
    } else {
        rx_buffer[rx_index++] = byte;
        if (rx_index >= sizeof(ProtocolMessage_t)) {
            receiving_frame = 0;
            process_received_frame((ProtocolMessage_t*)rx_buffer, huart);
        }
    }

    if (huart == &huart2) {
        HAL_UART_Receive_IT(&huart2, &rx2_byte, 1);
    } else if (huart == &huart1) {
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