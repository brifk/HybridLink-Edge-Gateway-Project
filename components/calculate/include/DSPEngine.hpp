#pragma once
#include "Thread.hpp"
#include "APPConfig.h"
#include "esp_log.h"
#include "esp_dsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bno055driver.hpp"
#include "model.hpp"
#include <memory>
#include <math.h>
#include <vector>

class DSPEngine : public Thread {
public:
    DSPEngine(std::shared_ptr<Bno055Driver> bno055) : 
            Thread("DSPEngine", 1024 * 3, PRIO_FFT, 1), 
            bno055(std::move(bno055)) {
                power_data.resize(N_SAMPLES / 2);
            };
    ~DSPEngine() = default;
    
    void run() override;
    int get_n_samples() { return N_SAMPLES; }

private:
    static constexpr auto TAG = "DSPEngine";
    static constexpr int N_SAMPLES = 256;
    static constexpr int N = N_SAMPLES;

    std::shared_ptr<Bno055Driver> bno055;
    
    alignas(16) float input_buffers_[2][N]; 
    int write_buffer_idx_ = 0;  // 当前正在写入哪个 buffer (0 或 1)
    int write_sample_idx_ = 0;  // 当前写到了第几个点
    alignas(16) float wind_[N_SAMPLES];         // 窗函数系数
    alignas(16) float y_cf_[N_SAMPLES * 2];     // 复数工作数组
    std::vector<double> power_data;
    bool fft_initialized_ = false;
    
    // TODO: 后面再实现对频谱的分析
    void processAndShow(float* data, int length);
};