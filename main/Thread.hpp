#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string>

// 抽象基类，任何任务都继承它
class Thread {
public:
    Thread(const char* name, uint32_t stackDepth, UBaseType_t priority, BaseType_t coreID)
        : m_name(name)
        , m_stackDepth(stackDepth)
        , m_priority(priority)
        , m_coreID(coreID)
    {
    }

    virtual ~Thread() { }

    // 启动任务
    void start()
    {
        xTaskCreatePinnedToCore(task_helper,
            m_name.c_str(),
            m_stackDepth,
            this,
            m_priority,
            &m_handle,
            m_coreID);
    }

protected:
    virtual void run() = 0;

private:
    // 静态中转函数
    static void task_helper(void* param)
    {
        Thread* thread = static_cast<Thread*>(param); // 恢复 this 指针
        thread->run(); // 进入真正的 C++ 成员函数
        // 任务结束后的清理
        vTaskDelete(NULL);
    }

    TaskHandle_t m_handle = nullptr;
    std::string m_name;
    uint32_t m_stackDepth;
    UBaseType_t m_priority;
    BaseType_t m_coreID;
};