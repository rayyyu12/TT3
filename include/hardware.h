#pragma once

#include "config.h"
#include <atomic>

#ifdef RASPI_HW
#include <wiringPi.h>
#include <mcp3008.h>
#endif

bool initialize_adc();
bool initialize_button(std::atomic<bool>& running, std::atomic<bool>& switch_requested);
void cleanup_gpio();

int read_adc_value();
float get_throttle_percentage(int raw_adc);

void start_adc_thread(std::atomic<bool>& running,
                      std::atomic<int>& adc_value_out);
void stop_adc_thread();

void apply_realtime_scheduling();
void pin_to_core(int core_id);
