//FreeRTOS Include
#include <FreeRTOS.h>
#include <stdint.h>
#include <task.h>
#include <queue.h>

//Std include
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


//Recovid include
#include "tasks_recovid.h"
#include "struct_recovid.h"


uint32_t get_time_ms()
{
  return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint32_t delay(uint32_t ticks_to_wait)
{
  if(xTaskGetCurrentTaskHandle() != NULL)
    vTaskDelay(ticks_to_wait);
#ifdef stm32f303
  else {
	HAL_Delay(ticks_to_wait);
  }
#endif
  return get_time_ms();
}

uint32_t wait_ms(uint32_t t_ms)
{
  if(xTaskGetCurrentTaskHandle() != NULL)
    vTaskDelay(t_ms/portTICK_PERIOD_MS);
#ifdef stm32f303
  else {
	HAL_Delay(t_ms*2/5);
  }
#endif
  return get_time_ms();
}

int sleepPeriodic(struct periodic_task* task)
{
  int ret = 0;
  if(xTaskGetTickCount() > (task->LastWakeTime + task->PeriodMs)) {
      ret =((xTaskGetTickCount() - (task->LastWakeTime + task->xFrequency))/task->xFrequency);
      //Skip the missed cycles by resetting the lastWakeTime
      task->LastWakeTime = xTaskGetTickCount();
  }
  vTaskDelayUntil( &task->LastWakeTime, task->PeriodMs);
  return ret;
}

int initTask(struct periodic_task* task)
{
  task->xFrequency = task->PeriodMs / portTICK_PERIOD_MS;
  printf("Task %s  started\n", task->name);
  return 0;
}

void vTimerCallback_Sensor_200Hz( TimerHandle_t xTimer )
{
	// TODO : get sensor value
}
void vTimerCallback_Sensor_1Hz( TimerHandle_t xTimer )
{
	// TODO : get sensor value
}

struct periodic_task task_array[] = {
  { TaskMessageManagement,         1,  "Message Management",  0, 0, 0},
  { TaskRespirationCycle,          1,  "Respiration Cycle",   0, 0, 0},
};

struct timer_task timer_array[] = {
  { 0, "SENSOR_200Hz",      5,     pdTRUE,   0, vTimerCallback_Sensor_200Hz},
  { 0, "SENSOR_1Hz",        1000,  pdTRUE,   0, vTimerCallback_Sensor_1Hz},
};

size_t size_task_array = sizeof(task_array) / sizeof(task_array[0]);
size_t size_timer_array = sizeof(timer_array) / sizeof(timer_array[0]);

