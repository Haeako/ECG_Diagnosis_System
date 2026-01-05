/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "header/wauxlib.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ad8232.h"
#include "string.h"
#include "usbd_cdc_if.h"
#include "math.h"
#include "ecg_data.h"
#include "HPfilter.h"
#include "SBfilter.h"
#include "Kalman.h"
#include "LPfilter.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
# define ECG_BUFFER_SIZE 1024
# define MAX_12BIT_ADC	4095
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */
#define PING_PONG_SIZE 20  // Mỗi lần đủ 20 mẫu thì gửi (tăng hiệu suất hơn 10)
double buffer0[PING_PONG_SIZE];
double buffer1[PING_PONG_SIZE];

double *write_ptr = buffer0; // Con trỏ để Interrupt ghi vào
double *read_ptr = NULL;    // Con trỏ để Main đọc ra
volatile uint32_t write_idx = 0;
volatile uint8_t buffer_full_flag = 0; // Cờ báo hiệu có buffer đã đầy

uint32_t ADCVal;
extern volatile int file_end_flag;
char tx_str_buffer[64]; // Đủ cho 1 mẫu

extern volatile int16_t rx_fifo[];
extern volatile uint32_t rx_w, rx_r;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
void send_cdc(char val);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
HPfilterType HPfilter;
SBfilterType SBFilter;
LPfilterType LPFilter;
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
//  char Tx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_TIM2_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start(&htim2);
  HAL_ADC_Start_DMA(&hadc1, &ADCVal, 1);
//  float pre_lp = 0.0f;
//  float	alpha = 0.02;
  HPfilter_init(&HPfilter);
  SBfilter_init(&SBFilter);
  LPfilter_init(&LPFilter);
  KalmanFilter_init(1.0f, 1.0f, 0.001f);
//  char msg[CDC_DATA_FS_MAX_PACKET_SIZE];
//  uint16_t msgLen = 0;
//	double *sig,*inp,*oup;
//	int J = 4, N, buffer_select = 0;
//	int buffer_index = 0;
//	char *wname = "d45";
//	char *ext = "per";// The other option sym is only available with "fft" cmethod
//	char *thresh = "soft";
//	char *cmethod = "direct";// The other option is "fft"
//	sig = (double*)malloc(sizeof(double)* N);
//	inp = (double*)malloc(sizeof(double)* N);
//	oup = (double*)malloc(sizeof(double)* N);
//	modwtshrink(sig,N,J,wname,cmethod,ext,thresh,oup);
  /* USER CODE END 2 */

  /* Infinite loop */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        // --- PHẦN 1: GỬI DỮ LIỆU QUA USB ---
        if (buffer_full_flag) {
            // Lưu lại con trỏ đang cần đọc để tránh Interrupt đổi giữa chừng
            double *data_to_send = read_ptr;

            for (int i = 0; i < PING_PONG_SIZE; i++) {
                // Dùng %.4f để độ chính xác cao hơn nếu cần
                int len = snprintf(tx_str_buffer, sizeof(tx_str_buffer), "%.2f\n", data_to_send[i]);
                if (len > 0) {
                    // Chờ cho đến khi USB sẵn sàng (tránh mất gói)
                    while(CDC_Transmit_FS((uint8_t*)tx_str_buffer, len) == USBD_BUSY);
                }
            }

            buffer_full_flag = 0; // Xong việc, hạ cờ
            HAL_GPIO_TogglePin(GPIOD, LD4_Pin); // Đèn xanh báo đang truyền
        }

        // --- PHẦN 2: XỬ LÝ KẾT THÚC FILE ---
        if (file_end_flag == 1 && rx_r == rx_w) {
            // Gửi nốt những gì còn sót trong buffer hiện tại (chưa đủ PING_PONG_SIZE)
            if (write_idx > 0) {
                for (int i = 0; i < write_idx; i++) {
                    int len = snprintf(tx_str_buffer, sizeof(tx_str_buffer), "%.2f\n", write_ptr[i]);
                    while(CDC_Transmit_FS((uint8_t*)tx_str_buffer, len) == USBD_BUSY);
                }
                write_idx = 0;
            }

            HAL_Delay(50); // Chờ USB đẩy hết buffer phần cứng

            // Reset bộ lọc cho file sau
            HPfilter_reset(&HPfilter);
            LPfilter_reset(&LPFilter);
            SBfilter_reset(&SBFilter);

            char *ok_msg = "OK\n";
            while(CDC_Transmit_FS((uint8_t*)ok_msg, strlen(ok_msg)) == USBD_BUSY);

            file_end_flag = 0;
            HAL_GPIO_TogglePin(GPIOD, LD5_Pin); // Đèn đỏ báo xong file
        }

        // Đèn Heartbeat
        if (HAL_GetTick() % 500 == 0)
            HAL_GPIO_TogglePin(GPIOD, LD3_Pin);
    }
    /* USER CODE END WHILE */
//  while (1)
//  {
//	    if (buffer_ready) {
//	        char *wname = "db4"; // db4 cho ECG
//
//	        // Gọi hàm lọc Wavelet
//	        modwtshrink((ecg_input + (ECG_BUFFER_SIZE * buffer_select)), ECG_BUFFER_SIZE, J, wname, "direct", "per", "soft", ecg_output);
//
//	        // Send data though USB CDC
//	        for(int i=0; i < ECG_BUFFER_SIZE; i++) {
//	            int len = sprintf(msg, "%.2f\r\n", ecg_output[i]);
//	            CDC_Transmit_FS((uint8_t*)msg, len);
//	            HAL_Delay(1);
//	        }
//
//	        buffer_ready = 0;
//	    }
//	  if (data) {
//		  data = 0;
////		  float raw_signal = (float)ADCVal;
//		  // 2. Đưa vào buffer cho Wavelet
////		  ecg_input[buffer_index++] = (double)raw_signal;
//		  ecg_input[(buffer_select * ECG_BUFFER_SIZE) + buffer_index++] = ecg_sim_data[sim_index++];
//		  if (sim_index >= SIM_DATA_LEN) break;
//		  if (buffer_index >= ECG_BUFFER_SIZE) {
//			  buffer_index = 0;
//			  buffer_ready = 1; // Set flag
//		  }
//	  }
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
//
//  }
  // end section
//  HAL_GPIO_WritePin(GPIOD,LD3_Pin, GPIO_PIN_SET);
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
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 167999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PDM_OUT_Pin */
  GPIO_InitStruct.Pin = PDM_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PDM_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : I2S3_WS_Pin */
  GPIO_InitStruct.Pin = I2S3_WS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(I2S3_WS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : SPI1_SCK_Pin SPI1_MISO_Pin SPI1_MOSI_Pin */
  GPIO_InitStruct.Pin = SPI1_SCK_Pin|SPI1_MISO_Pin|SPI1_MOSI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD6_Pin
                           Audio_RST_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : I2S3_MCK_Pin I2S3_SCK_Pin I2S3_SD_Pin */
  GPIO_InitStruct.Pin = I2S3_MCK_Pin|I2S3_SCK_Pin|I2S3_SD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : Audio_SCL_Pin Audio_SDA_Pin */
  GPIO_InitStruct.Pin = Audio_SCL_Pin|Audio_SDA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// Callback này chạy theo tần số của Timer 2 (ví dụ 500Hz)
/* USER CODE BEGIN 4 */
/* USER CODE BEGIN 4 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if(hadc->Instance == ADC1)
    {
        if (rx_r != rx_w) {
            int16_t raw_sample = rx_fifo[rx_r];
            rx_r = (rx_r + 1) % RX_FIFO_SIZE;
            float val = (float)raw_sample;

            // --- CHUỖI LỌC ---
            HPfilter_writeInput(&HPfilter, val);
            val = HPfilter_readOutput(&HPfilter);
            SBfilter_writeInput(&SBFilter, val);
            val = SBfilter_readOutput(&SBFilter);
            LPfilter_writeInput(&LPFilter, val);
            val = LPfilter_readOutput(&LPFilter);
            val = KFupdateEstimate(val);

            // --- GHI VÀO PING-PONG BUFFER ---
            write_ptr[write_idx++] = (double)val;

            if (write_idx >= PING_PONG_SIZE) {
                // Nếu Main chưa xử lý xong buffer cũ (tràn), ta buộc phải ghi đè
                // hoặc dừng. Ở đây ta ưu tiên đổi buffer để tránh mất mẫu.
                read_ptr = write_ptr; // Gán buffer vừa đầy cho Main

                // Đổi buffer ghi (Ping <-> Pong)
                if (write_ptr == buffer0) write_ptr = buffer1;
                else write_ptr = buffer0;

                write_idx = 0;
                buffer_full_flag = 1; // Báo Main "Cơm chín rồi, ăn đi"
            }
        }
    }
}
/* USER CODE END 4 */
/* USER CODE END 4 */
//
//void CDC_ReceiveCallback(uint8_t *Buf, uint32_t Len)
//{
//    static int val = 0;
//    static int sign = 1;
//
//    for (uint32_t i = 0; i < Len; i++) {
//        char c = Buf[i];
//
//        if (c == '-') {
//            sign = -1;
//        }
//        else if (c >= '0' && c <= '9') {
//            val = val * 10 + (c - '0');
//        }
//        else if (c == '\n') {
//            uint32_t next = (rx_w + 1) % RX_FIFO_SIZE;
//            if (next != rx_r) {           // FIFO not full
//                rx_fifo[rx_w] = sign * val;
//                rx_w = next;
//            }
//            val = 0;
//            sign = 1;
//        }
//    }
//}
//

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
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
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
