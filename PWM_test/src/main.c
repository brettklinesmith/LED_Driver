/**
 ******************************************************************************
 * File Name          : main.c
 * Description        : Main program body
 ******************************************************************************
 ** This notice applies to any and all portions of this file
 * that are not between comment pairs USER CODE BEGIN and
 * USER CODE END. Other portions of this file, whether
 * inserted by the user or by software development tools
 * are owned by their respective copyright owners.
 *
 * COPYRIGHT(c) 2018 STMicroelectronics
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f1xx_hal.h"

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef 	htim3;
volatile uint8_t 	updatePWM 		= 1,		//basically a bool
					changePattern	= 0;
volatile uint32_t 	currentTicks	= 0,		//value of SYSTICK
					updateTicks 	= 1,	//value of next PWM update
					nextUpdateTime 	= 1,	//Duration of current output
					fadeTicks		= 0,		//Duration of fade
					endOfFade		= 0,		//Systick value at end of fade
					stateTicks		= 0,		//Duration of state
					endOfState		= 0;		//Systick value at end of state

/* Initialize patterns. Basically a Malloc() of existing pattern memory
 *
 * A single pattern STEP requires two 32-bit registers. The have the form:
 * [red 10-bit value]   [green 10-bit value] [blue 10-bit value]  0x01
 * [white 10-bit value] [fade 10-bit value]  [dwell 10-bit value] 0x10
 * the last element in the pattern has the command bit (2 LSB bits) * of 11 to
 * signify EOF.
 *
 * Fade and dwell times have 2 components. The two MSB bits define
 * the time base:
 * 		00 = 100ms 		range of    0:25.5 -    0:00.1
 * 		01 = seconds	range of    4:15.0 -    0:01.0
 * 		10 = 5 seconds	range of   21:15.0 -    0:05.0
 * 		11 = minutes	range of 4:15:00.0 - 0:01:00.0
 *
 * The eight LSB bits of the time values define the actual value in the form:
 *
 * 		[8-bit value] * timebase
 *
 * For a pattern of greater resolution multiple pattern values should be used
 * with the same color values, a fade of 100 ms, and a the necessary dwell
 * time to achieve the desired total dwell.
 *
 * 		ie. for 5 minutes and 17.6 seconds
 * 			10 * 3C   //5 minutes
 * 			00 * B0   //17.6 seconds
 */
static __attribute__((section("PATTERN_1"))) const volatile uint32_t pattern1 [200];
static __attribute__((section("PATTERN_2"))) const volatile uint32_t pattern2 [200];
static __attribute__((section("PATTERN_3"))) const volatile uint32_t pattern3 [200];
static __attribute__((section("PATTERN_4"))) const volatile uint32_t pattern4 [200];
static __attribute__((section("PATTERN_5"))) const volatile uint32_t pattern5 [200];

const volatile uint32_t *patternAddess[5] = {&pattern1,&pattern2,&pattern3,&pattern4,&pattern5};
uint8_t patternIndex = 0;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void SYSTICK_Init(void);
void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
void set_pwm_value(uint16_t[4]);
void decodeState(uint32_t,uint32_t,uint16_t *);
uint16_t decodeTime(uint16_t);


int main(void) {

	// Reset of all peripherals, Initializes the Flash interface and the Systick.
	HAL_Init();

	// Configure the system clock
	SystemClock_Config();

	// Initialize all configured peripherals
	MX_GPIO_Init();
	MX_TIM3_Init();
	SYSTICK_Init();

	HAL_TIM_Base_Start(&htim3);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

	uint16_t 	PWMPeriods[4]   = {0,0,0,0},
	            targetPeriods[4]= {0,0,0,0},
	            fadeOngoing     = 0,
	            fadeStepTicks   = 10;   //TODO: Should calculate this better handle integer division

	int16_t     offsetPeriods[4] = {0,0,0,0};

	uint32_t	*nextState,
				stateFirst,
				stateSecond;

	nextState = patternAddess[patternIndex];

	while (1) {
			/* When updatePWM has a value of 1 the SysTick interrupt has determined
			 * that current state time has elapsed. Current PWM values are updated
			 * immediately and then the next value is computed.
			 */
			if(updatePWM != 0){

				if(fadeOngoing == 1) {
				    //most cycles go here

				    if((HAL_GetTick()) > endOfFade){  //last step of fade
	    			    PWMPeriods[0] = targetPeriods[0];
	    			    PWMPeriods[1] = targetPeriods[1];
	    			    PWMPeriods[2] = targetPeriods[2];
	    			    PWMPeriods[3] = targetPeriods[3];

				        fadeOngoing = 0;
				        updateTicks = endOfState;
				    } else {                                            //middle step of fade
	    			    PWMPeriods[0] += offsetPeriods[0];
	    			    PWMPeriods[1] += offsetPeriods[1];
	    			    PWMPeriods[2] += offsetPeriods[2];
	    			    PWMPeriods[3] += offsetPeriods[3];

	    			    nextUpdateTime = fadeStepTicks;
				    }
				} else {
				    //should only run once per state at the very end of the fade

	    			stateFirst = *nextState;
	    			nextState++;
	    			stateSecond = *nextState;

	    			//check for EoF
	    			if((stateSecond & 0x3) != 0x3){
	    				nextState++;
	    			} else {
	    				nextState = patternAddess[patternIndex];
	    			}

	    			decodeState(stateFirst,stateSecond,&targetPeriods);
	    			offsetPeriods[0] = ((int16_t) targetPeriods[0] - (int16_t) PWMPeriods[0]) / (int16_t) (fadeTicks/fadeStepTicks);
	    			offsetPeriods[1] = ((int16_t) targetPeriods[1] - (int16_t) PWMPeriods[1]) / (int16_t) (fadeTicks/fadeStepTicks);
	    			offsetPeriods[2] = ((int16_t) targetPeriods[2] - (int16_t) PWMPeriods[2]) / (int16_t) (fadeTicks/fadeStepTicks);
	    			offsetPeriods[3] = ((int16_t) targetPeriods[3] - (int16_t) PWMPeriods[3]) / (int16_t) (fadeTicks/fadeStepTicks);

			        fadeOngoing = 1;

	    			nextUpdateTime = fadeStepTicks;
				}

				set_pwm_value(PWMPeriods);

	    		updatePWM = 0;
			}

			if(changePattern == 1){
				if(patternIndex < 4){
					patternIndex ++;
				} else {
					patternIndex = 0;
				}

				nextState = patternAddess[patternIndex];
				fadeOngoing = 0;
				updatePWM = 1;

				changePattern = 0;
			}
		}

}

/** System Clock Configuration
 */
void SystemClock_Config(void) {

	RCC_OscInitTypeDef RCC_OscInitStruct;
	RCC_ClkInitTypeDef RCC_ClkInitStruct;

	// Initializes the CPU, AHB and APB busses clocks
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = 16;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	// Initializes the CPU, AHB and APB busses clocks
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	// Configure the Systick interrupt time
	HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);

	// Configure the Systick
	HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

	// SysTick_IRQn interrupt configuration
	HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* TIM3 init function */
static void MX_TIM3_Init(void) {

	TIM_ClockConfigTypeDef sClockSourceConfig;
	TIM_MasterConfigTypeDef sMasterConfig;
	TIM_OC_InitTypeDef sConfigOC;

	htim3.Instance = TIM3;
	htim3.Init.Prescaler = 0;
	htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
	htim3.Init.Period = 0xFFF;
	htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
	htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
	if (HAL_TIM_Base_Init(&htim3) != HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
	if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
	sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
	if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sConfigOC.OCMode = TIM_OCMODE_PWM1;
	sConfigOC.Pulse = 0x000;
	sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
	sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sConfigOC.Pulse = 0x000;
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sConfigOC.Pulse = 0x000;
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sConfigOC.Pulse = 0x000;
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	HAL_TIM_MspPostInit(&htim3);

}

/** Configure pins as 
 * Analog
 * Input
 * Output
 * EVENT_OUT
 * EXTI
 PA2   ------> USART2_TX
 PA3   ------> USART2_RX
 */
static void MX_GPIO_Init(void) {

	GPIO_InitTypeDef GPIO_InitStruct;

	// GPIO Ports Clock Enable
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();

	// Configure GPIO pin Output Level
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

	// Configure GPIO pin : B1_Pin
	GPIO_InitStruct.Pin = B1_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

	// Configure GPIO pins : USART_TX_Pin USART_RX_Pin
	GPIO_InitStruct.Pin = USART_TX_Pin | USART_RX_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	// Configure GPIO pin : LD2_Pin
	GPIO_InitStruct.Pin = LD2_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

	// EXTI interrupt init
	HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

}

static void SYSTICK_Init(void) {
	HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

	if (HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000) != HAL_OK) {      	//Configure SysTick to generate an interrupt every millisecond
			_Error_Handler(__FILE__, __LINE__);
		}
}


void set_pwm_value(uint16_t periods[4]){
    TIM_OC_InitTypeDef sConfigOC;

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = periods[0];
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1)
    		!= HAL_OK) {
    	_Error_Handler(__FILE__, __LINE__);
    }

	sConfigOC.Pulse = periods[1];
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sConfigOC.Pulse = periods[2];
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

	sConfigOC.Pulse = periods[3];
	if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4)
			!= HAL_OK) {
		_Error_Handler(__FILE__, __LINE__);
	}

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
}


/* TODO: 10-bit values are copied directly to the output, should << 2
 *  for final version. Leaving it this way for now to reduce intensity */
void decodeState(uint32_t first,uint32_t second,uint16_t  *periods){

	*periods = ((first  & 0xFFC00000) >> 20);
	periods++;
	*periods= ((first  & 0x003FF000) >> 10);
	periods++;
	*periods = ((first  & 0x00000FFC) >> 0);
	periods++;
	*periods = ((second & 0xFFC00000) >> 20);

	stateTicks = decodeTime((second & 0xFFC) >> 2);
	fadeTicks  = decodeTime((second & 0x3FF000) >> 12);

	endOfFade  = HAL_GetTick() + fadeTicks;
	endOfState = endOfFade + stateTicks;
}

uint16_t decodeTime(uint16_t time){
	int timeBase;
	timeBase = ((time & 0x300) >> 8);

	switch(timeBase){
	case 0x0:
		return(100 * (time & 0xFF));
		break;

	case 0x1:
		return(1000 * (time & 0xFF));
		break;

	case 0x2:
		return(5000 * (time & 0xFF));
		break;

	case 0x3:
		return(60000 * (time & 0xFF));
		break;
	}

	return(0);
}

void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */
  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  currentTicks = HAL_GetTick();
  if(currentTicks >= updateTicks){
	  updatePWM = 1;
	  updateTicks += nextUpdateTime;
  }

  /* USER CODE END SysTick_IRQn 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @param  None
 * @retval None
 */
void _Error_Handler(char * file, int line) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	while (1) {
	}
	/* USER CODE END Error_Handler_Debug */
}

void EXTI15_10_IRQHandler(void) {
	__HAL_GPIO_EXTI_CLEAR_IT(B1_Pin);
	changePattern = 1;
}

#ifdef USE_FULL_ASSERT

/**
 * @brief Reports the name of the source file and the source line number
 * where the assert_param error has occurred.
 * @param file: pointer to the source file name
 * @param line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	/* USER CODE END 6 */

}

#endif

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
