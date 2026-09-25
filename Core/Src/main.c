/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
	uint16_t ir_sample;
	uint16_t red_sample;
} sampleData_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
// Endereço do dispositivo I2C
#define MAX30100_DEVICE_ADDRESS  (0x57<<1)

// Endereços dos registradores internos
#define MAX30100_INTERRUPT_STATUS 0x00
#define MAX30100_INTERRUPT_ENABLE 0x01
#define MAX30100_FIFO_WR_PTR	  0x02
#define MAX30100_OVF_COUNTER	  0x03
#define MAX30100_FIFO_RD_PTR	  0x04
#define MAX30100_FIFO_DATA        0x05
#define MAX30100_CONFIG_MODE      0x06
#define MAX30100_SPO2_CONFIG      0x07
#define MAX30100_LED_CONFIG       0x09

// Configurações
#define MAX30100_INT_EN_A_FULL   (1<<7)		// Interrupção habilitada - Almost Full
#define MAX30100_MODE_HR_EN       0x02		// Configuração modo apenas HeartRate
#define MAX30100_MODE_SPO2_HR_EN  0x03		// Configuração modo HeartRate + SpO2
#define MAX30100_SPO2_16BITS      0x03		// 50 samples/S, pulse-width 1600us, resolução 16bits
#define MAX30100_LED_CURRENT_CTRL 0x33		// Controle corrente LED IR+R com 11 mA cada
#define MAX30100_CLEAR			  0x00		// Limpa registradores -> seta em zero

// Endereços verificação
#define MAX30100_PWR_RDY 		  0x01
#define MAX30100_PART_ID 		  0xFF


#define MAX30100_MAX_DELAY		  10
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
// Macro para calcular comprimento do array
#define LEN_ARRAY(array)	(sizeof(array) / sizeof((array)[0]))
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
DMA_HandleTypeDef hdma_i2c1_rx;
DMA_HandleTypeDef hdma_i2c1_tx;

/* USER CODE BEGIN PV */
uint8_t devices[4] = {0};
uint8_t *ptrDev = devices;

uint8_t fifo_buffer[64];
uint8_t *ptrFifo_buffer = fifo_buffer;

uint8_t usbData = 0;

sampleData_t rawSample;

//uint16_t ir_sample = 0;
//uint16_t red_sample = 0;

HAL_StatusTypeDef cfgOk;

volatile uint8_t dmaTransferActive = 0;
volatile uint8_t rxCplt = 0;
volatile uint8_t newInterrupt = 0;

volatile USBD_StatusTypeDef usbTxCplt = USBD_OK;

uint8_t samplesSize = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */
extern uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);
void I2C_Bus_Clear(void);
HAL_StatusTypeDef max30100_init();
HAL_StatusTypeDef max30100_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t regAddr, uint8_t modeCfg);
HAL_StatusTypeDef max30100_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t regAddr, uint8_t *pValue);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  I2C_Bus_Clear();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */

  // Verificação se o microcontrolador é Big-Endian ou Little-Endian
//  uint32_t verifEndianess = 0x0A0B0C0D;
//  uint8_t *ptrVerEndianess;
//  ptrVerEndianess = &verifEndianess;

  // Verificação de dispositivos I2C disponíveis
//  for(uint8_t i = 0; i <128; i++){
//	  if(HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(i << 1), 3, 5) == HAL_OK){
//		  *ptrDev = i;
//		  ptrDev++;
//	  }
//  }

  while(max30100_init() != HAL_OK){
	  HAL_Delay(50);
  }


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if(newInterrupt){
		  newInterrupt = 0;
		  uint8_t fifoRdPtr = 0, fifoWrPtr = 0;
		  int8_t numSamples = 0;
		  static uint8_t statusReg = 0;
		  if(HAL_I2C_Mem_Read(&hi2c1, MAX30100_DEVICE_ADDRESS, MAX30100_INTERRUPT_STATUS, I2C_MEMADD_SIZE_8BIT, &statusReg, sizeof(statusReg), MAX30100_MAX_DELAY) == HAL_OK){
			  if(statusReg && 0x01){
				  I2C_Bus_Clear();
				  max30100_init();
			  } else {
				  max30100_ReadReg(&hi2c1, MAX30100_FIFO_WR_PTR, &fifoWrPtr);
				  max30100_ReadReg(&hi2c1, MAX30100_FIFO_RD_PTR, &fifoRdPtr);

				  numSamples = (fifoWrPtr - fifoRdPtr) & 0x0F;
				  if(numSamples <= 0){
					  numSamples = 16;
				  }
				  samplesSize = numSamples * 4;
				  //
				  dmaTransferActive = 1;	// Flag para verificação no callback
				  HAL_I2C_Mem_Read_DMA(&hi2c1, MAX30100_DEVICE_ADDRESS, MAX30100_FIFO_DATA, I2C_MEMADD_SIZE_8BIT, ptrFifo_buffer, samplesSize);
			  }
		  }
	  }
	  if(rxCplt){
		  rxCplt = 0;
		  uint16_t txLen = 0;
		  uint32_t timeout = 0;
		  char msgBuffer[64];
		  for(uint8_t i = 0; i < samplesSize; i+=4){
			  // Carrega os dados do fifo_buffer nas variáveis da struct
			  rawSample.ir_sample  = fifo_buffer[i+0]<<8 | fifo_buffer[i+1];
			  rawSample.red_sample = fifo_buffer[i+2]<<8 | fifo_buffer[i+3];
			  // Carrega os valores das amostras em uma string e a função sprintf retorna o tamanho alocado
			  txLen = sprintf(msgBuffer, "ir: %u, red: %u\n", rawSample.ir_sample, rawSample.red_sample);
			  // Testa se a flag está livre, se não estiver aguarda até o timeout predefinido
			  timeout = HAL_GetTick();
			  while(usbTxCplt != USBD_OK){
				  if(HAL_GetTick() - timeout > MAX30100_MAX_DELAY){
					  break; 	// Se chegou no timeout, o laço é interrompido e segue o código, mesmo com a USB travada/ desconectada
				  }
			  }
			  // Marca ocupado e envia nova amostra
			  usbTxCplt = USBD_BUSY;
			  if(CDC_Transmit_FS((uint8_t*)msgBuffer, txLen) != USBD_OK){ // Se o envio falhar, reseta a flag para nova tentativa
				  usbTxCplt = USBD_OK;
			  }

		  }



    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  }
  /* USER CODE END 3 */
  }
}
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 15;
  RCC_OscInitStruct.PLL.PLLN = 144;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 5;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA9 */
  GPIO_InitStruct.Pin = GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
HAL_StatusTypeDef max30100_init(){
    uint8_t part_id = 0;
    uint8_t dummyStatus = 0;
    HAL_StatusTypeDef status;

    HAL_Delay(100);		// Garantir estabilização da alimentação
    status = HAL_I2C_Mem_Read(&hi2c1, MAX30100_DEVICE_ADDRESS, MAX30100_PART_ID, I2C_MEMADD_SIZE_8BIT, &part_id, sizeof(part_id), MAX30100_MAX_DELAY);
    if(status != HAL_OK || part_id != 0x11){
    	return HAL_ERROR;
    }
    // Reset ponteiros da FIFO
    max30100_WriteReg(&hi2c1, MAX30100_OVF_COUNTER, MAX30100_CLEAR);
	max30100_WriteReg(&hi2c1, MAX30100_FIFO_WR_PTR, MAX30100_CLEAR);
	max30100_WriteReg(&hi2c1, MAX30100_FIFO_RD_PTR, MAX30100_CLEAR);

    // Configurações dos registradores
    max30100_WriteReg(&hi2c1, MAX30100_CONFIG_MODE, MAX30100_MODE_SPO2_HR_EN);
    max30100_WriteReg(&hi2c1, MAX30100_SPO2_CONFIG, MAX30100_SPO2_16BITS);
    max30100_WriteReg(&hi2c1, MAX30100_LED_CONFIG, MAX30100_LED_CURRENT_CTRL);

    // Habilita interrupção Almost Full
    max30100_WriteReg(&hi2c1, MAX30100_INTERRUPT_ENABLE, MAX30100_INT_EN_A_FULL);

    // Leitura Interrupt Status para limpar os registradores
    max30100_ReadReg(&hi2c1, MAX30100_INTERRUPT_STATUS, &dummyStatus);


    return HAL_OK;
}

HAL_StatusTypeDef max30100_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t regAddr, uint8_t modeCfg){
	//HAL_I2C_Mem_Write(hi2c, DevAddress, MemAddress, MemAddSize, pData, Size, Timeout);
	return HAL_I2C_Mem_Write(hi2c, MAX30100_DEVICE_ADDRESS, regAddr, I2C_MEMADD_SIZE_8BIT, &modeCfg, sizeof(modeCfg), MAX30100_MAX_DELAY);
}

HAL_StatusTypeDef max30100_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t regAddr, uint8_t *pValue){
	return HAL_I2C_Mem_Read(hi2c, MAX30100_DEVICE_ADDRESS, regAddr, I2C_MEMADD_SIZE_8BIT, pValue, 1, MAX30100_MAX_DELAY);

}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin){
	if(GPIO_Pin == GPIO_PIN_9){
		newInterrupt = 1;
	}

}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef* hi2c){
	if(hi2c->Instance == I2C1){
		if(dmaTransferActive){
			dmaTransferActive = 0;
			static uint8_t dummyStatus = 0;
			HAL_I2C_Mem_Read(&hi2c1, MAX30100_DEVICE_ADDRESS, MAX30100_INTERRUPT_STATUS, I2C_MEMADD_SIZE_8BIT, &dummyStatus, sizeof(dummyStatus), MAX30100_MAX_DELAY);
			rxCplt = 1;
		}
	}
}


void I2C_Bus_Clear(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. Habilita o clock do GPIO do I2C1 (assumindo PB6 = SCL, PB7 = SDA)
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 2. Configura PB6 (SCL) e PB7 (SDA) como Open-Drain Output manual
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // Garante as linhas em nível alto inicialmente
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);

    // 3. Envia 9 pulsos de clock no SCL para o slave (MAX30100) soltar a linha SDA
    for (uint8_t i = 0; i < 9; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        for (volatile int d = 0; d < 100; d++); // Pequeno atraso
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        for (volatile int d = 0; d < 100; d++);
    }

    // 4. Envia condição de STOP manual
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    for (volatile int d = 0; d < 100; d++);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    for (volatile int d = 0; d < 100; d++);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
	  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	  HAL_Delay(5000);
	  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
	  HAL_Delay(5000);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
