#include "hardware.h"
#include <atomic>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

#ifdef RASPI_HW
#include <wiringPi.h>
#include <mcp3008.h>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

static constexpr int MCP3008_BASE = 100;
static constexpr int SPI_CHANNEL  = 0;

// --------------------------------------------------------------------------
// ADC polling thread
// --------------------------------------------------------------------------

static std::thread adc_thread_handle;

void start_adc_thread(std::atomic<bool>& running,
                      std::atomic<int>& adc_value_out)
{
    adc_thread_handle = std::thread([&running, &adc_value_out]() {
        pin_to_core(2);

        constexpr auto poll_interval = std::chrono::microseconds(8333); // ~120 Hz
        while (running.load(std::memory_order_relaxed)) {
            int val = analogRead(MCP3008_BASE + ADC_CHANNEL_NUMBER);
            adc_value_out.store(val, std::memory_order_relaxed);
            std::this_thread::sleep_for(poll_interval);
        }
    });
    std::printf("ADC polling thread started on core 2 (~120 Hz)\n");
}

void stop_adc_thread() {
    if (adc_thread_handle.joinable())
        adc_thread_handle.join();
}

// --------------------------------------------------------------------------
// Interrupt-driven button via wiringPi ISR
// --------------------------------------------------------------------------

static std::atomic<bool>* g_running_ptr       = nullptr;
static std::atomic<bool>* g_switch_req_ptr    = nullptr;
static std::atomic<float> g_button_press_time{0.0f};

static float monotonic_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<float>(ts.tv_sec) + static_cast<float>(ts.tv_nsec) * 1e-9f;
}

static void button_isr() {
    bool pressed = !digitalRead(BUTTON_GPIO_PIN);
    float now = monotonic_sec();

    if (pressed) {
        g_button_press_time.store(now, std::memory_order_relaxed);
    } else {
        float start = g_button_press_time.exchange(0.0f, std::memory_order_relaxed);
        if (start <= 0.0f) return;
        float duration = now - start;

        if (duration >= BUTTON_LONG_PRESS_TIME) {
            if (g_running_ptr)
                g_running_ptr->store(false, std::memory_order_release);
        } else if (duration >= BUTTON_DEBOUNCE_TIME) {
            if (g_switch_req_ptr)
                g_switch_req_ptr->store(true, std::memory_order_release);
        }
    }
}

bool initialize_adc() {
    if (mcp3008Setup(MCP3008_BASE, SPI_CHANNEL) == -1) {
        std::printf("FATAL ERROR initializing ADC via mcp3008Setup\n");
        return false;
    }
    std::printf("MCP3008 ADC initialized on channel P%d.\n", ADC_CHANNEL_NUMBER);
    return true;
}

bool initialize_button(std::atomic<bool>& running,
                       std::atomic<bool>& switch_requested)
{
    wiringPiSetupGpio();
    pinMode(BUTTON_GPIO_PIN, INPUT);
    pullUpDnControl(BUTTON_GPIO_PIN, PUD_UP);

    g_running_ptr    = &running;
    g_switch_req_ptr = &switch_requested;

    if (wiringPiISR(BUTTON_GPIO_PIN, INT_EDGE_BOTH, &button_isr) < 0) {
        std::printf("WARNING: ISR registration failed, button disabled.\n");
        return false;
    }

    std::printf("Button initialized on GPIO %d (interrupt-driven)\n", BUTTON_GPIO_PIN);
    return true;
}

void cleanup_gpio() {
    g_running_ptr    = nullptr;
    g_switch_req_ptr = nullptr;
    std::printf("GPIO cleanup completed.\n");
}

int read_adc_value() {
    return analogRead(MCP3008_BASE + ADC_CHANNEL_NUMBER);
}

// --------------------------------------------------------------------------
// Real-time scheduling: SCHED_FIFO + mlockall
// --------------------------------------------------------------------------

void apply_realtime_scheduling() {
    struct sched_param sp{};
    sp.sched_priority = 49;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0) {
        std::printf("Real-time scheduling (SCHED_FIFO, priority %d) applied.\n",
                    sp.sched_priority);
    } else {
        std::printf("WARNING: Could not set SCHED_FIFO (run as root or use 'nice -n -20').\n");
    }

    if (mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        std::printf("Memory locked (mlockall) to prevent page faults.\n");
    } else {
        std::printf("WARNING: mlockall failed. Page faults may occur.\n");
    }
}

// --------------------------------------------------------------------------
// CPU core pinning
// --------------------------------------------------------------------------

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0) {
        std::printf("Thread pinned to core %d.\n", core_id);
    } else {
        std::printf("WARNING: Could not pin to core %d.\n", core_id);
    }
}

#else  // No Raspberry Pi hardware -----------------------------------------------

bool initialize_adc() {
    std::printf("ADC hardware modules not available. Cannot initialize ADC.\n");
    return false;
}

bool initialize_button(std::atomic<bool>& /*running*/,
                       std::atomic<bool>& /*switch_requested*/)
{
    std::printf("GPIO hardware not available. Button disabled.\n");
    return false;
}

void cleanup_gpio() {}

int read_adc_value() {
    return MIN_ADC_VALUE;
}

void start_adc_thread(std::atomic<bool>& /*running*/,
                      std::atomic<int>& /*adc_value_out*/) {}

void stop_adc_thread() {}

void apply_realtime_scheduling() {
    std::printf("Real-time scheduling not available (not Raspberry Pi).\n");
}

void pin_to_core(int /*core_id*/) {}

#endif  // RASPI_HW

float get_throttle_percentage(int raw_adc) {
    if (MAX_ADC_VALUE == MIN_ADC_VALUE) return 0.0f;
    int clamped = std::clamp(raw_adc, MIN_ADC_VALUE, MAX_ADC_VALUE);
    return static_cast<float>(clamped - MIN_ADC_VALUE)
         / static_cast<float>(MAX_ADC_VALUE - MIN_ADC_VALUE);
}
