#pragma once

#include "config.h"
#include <atomic>

#ifdef RASPI_HW
#include <wiringPi.h>
#include <mcp3008.h>
#endif

bool initialize_adc();
bool initialize_button(std::atomic<bool>& running, std::atomic<bool>& switch_requested);
bool initialize_signal_buttons();
void cleanup_gpio();

int read_adc_value();
float get_throttle_percentage(int raw_adc);

// Edge-triggered signal button events. ISR sets the flag; consumer atomically
// exchanges to false. Used by the SVJ engine for upshift / downshift / mode.
bool consume_left_signal_event();
bool consume_right_signal_event();

// Desktop builds (no Pi hardware) set these from keyboard polling so the rest
// of the codebase can use the same consume_*_signal_event() interface.
void post_left_signal_event();
void post_right_signal_event();

void start_adc_thread(std::atomic<bool>& running,
                      std::atomic<int>& adc_value_out);
void stop_adc_thread();

void apply_realtime_scheduling();
void pin_to_core(int core_id);
