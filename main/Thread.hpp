#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

// 抽象基类，任何任务都继承它
class Thread {
public:
    Thread(const char* name, uint32_t stackDepth, UBaseType_t priority) 
        : m_name(name), m_stackDepth(stackDepth), m_priority(priority) {}

    virtual ~Thread() {}

    // 启动任务
    void start() {
        // xTaskCreate 需要一个 C 函数指针，我们传入静态函数 helper
        xTaskCreate(task_helper, m_name.c_str(), m_stackDepth, this, m_priority, &m_handle);
    }

protected:
    virtual void run() = 0;

private:
    // 静态中转函数
    static void task_helper(void* param) {
        Thread* thread = static_cast<Thread*>(param); // 恢复 this 指针
        thread->run(); // 进入真正的 C++ 成员函数
        
        // 任务结束后的清理（如果 run 退出了）
        vTaskDelete(NULL);
    }

    TaskHandle_t m_handle = nullptr;
    std::string m_name;
    uint32_t m_stackDepth;
    UBaseType_t m_priority;
};