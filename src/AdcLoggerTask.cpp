#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AdcLoggerTask.h"
#include "led25.h"
#include "hardware/adc.h"
#include "utility.h"
#include "CommandTask.h"
void adc_task(void *param){
    auto* logger = static_cast<EventLogger*>(param);

    adc_init();
    adc_gpio_init(26);  // GPIO26 = ADC0
    adc_select_input(0);
    led_init();
    MovingAverage<uint16_t, 16> adc_avg;
    while(true){
        
        uint16_t raw = adc_read();
        adc_avg.add(raw);
        uint16_t raw_avg = adc_avg.average();
        float voltage = static_cast<float>(raw_avg) * 3.3f / 4095.0;
        if(!isLogPaused()){
            led_sw();
            logger->logf(LogLevel::INFO, "ADC raw=%u avg=%u voltage=%.3f",
                static_cast<unsigned>(raw), static_cast<unsigned>(raw_avg), static_cast<double>(voltage));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}




