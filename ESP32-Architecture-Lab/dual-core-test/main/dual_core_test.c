#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <math.h>

// Inter-core communication
static QueueHandle_t core_queue;
static SemaphoreHandle_t print_mutex;

// Performance counters
static volatile uint32_t core0_counter = 0;
static volatile uint32_t core1_counter = 0;
static volatile uint64_t core0_total_time = 0;
static volatile uint64_t core1_total_time = 0;

// Message structure for inter-core communication
typedef struct {
    uint32_t sender_core;
    uint32_t message_id;
    uint64_t timestamp;
    char data[32];
} core_message_t;

void safe_printf(const char* format, ...) {
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    xSemaphoreGive(print_mutex);
}

// Task for Core 0 (PRO_CPU)
void core0_task(void *parameter) {
    core_message_t message;
    uint64_t task_start = esp_timer_get_time();
    
    safe_printf("Core 0 Task Started (PRO_CPU)\n");
    
    for(int i = 0; i < 100; i++) {
        uint64_t iteration_start = esp_timer_get_time();
        
        // Simulate protocol processing work
        uint32_t checksum = 0;
        for(int j = 0; j < 1000; j++) {
            checksum += j * 997;  // Some computation
        }
        
        // Send message to Core 1 every 10 iterations
        if(i % 10 == 0) {
            message.sender_core = 0;
            message.message_id = i;
            message.timestamp = esp_timer_get_time();
            snprintf(message.data, sizeof(message.data), "Hello from Core 0 #%d", i);
            
            if(xQueueSend(core_queue, &message, pdMS_TO_TICKS(100)) == pdTRUE) {
                safe_printf("Core 0: Sent message %d\n", i);
            }
        }
        
        core0_counter++;
        uint64_t iteration_time = esp_timer_get_time() - iteration_start;
        core0_total_time += iteration_time;
        
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms delay
    }
    
    uint64_t task_end = esp_timer_get_time();
    safe_printf("Core 0 Task Completed in %llu ms\n", (task_end - task_start) / 1000);
    vTaskDelete(NULL);
}

// Task for Core 1 (APP_CPU)
void core1_task(void *parameter) {
    core_message_t received_message;
    uint64_t task_start = esp_timer_get_time();
    
    safe_printf("Core 1 Task Started (APP_CPU)\n");
    
    for(int i = 0; i < 150; i++) {
        uint64_t iteration_start = esp_timer_get_time();
        
        // Simulate application processing work
        float result = 0.0;
        for(int j = 0; j < 500; j++) {
            result += sqrt(j * 1.7f);  // Floating point computation
        }
        
        // Check for messages from Core 0
        if(xQueueReceive(core_queue, &received_message, pdMS_TO_TICKS(10)) == pdTRUE) {
            uint64_t latency = esp_timer_get_time() - received_message.timestamp;
            safe_printf("Core 1: Received '%s' (latency: %llu μs)\n", 
                       received_message.data, latency);
        }
        
        core1_counter++;
        uint64_t iteration_time = esp_timer_get_time() - iteration_start;
        core1_total_time += iteration_time;
        
        vTaskDelay(pdMS_TO_TICKS(30));  // 30ms delay
    }
    
    uint64_t task_end = esp_timer_get_time();
    safe_printf("Core 1 Task Completed in %llu ms\n", (task_end - task_start) / 1000);
    vTaskDelete(NULL);
}

// Monitoring task (can run on either core)
void monitor_task(void *parameter) {
    TickType_t last_wake_time = xTaskGetTickCount();
    
    for(int i = 0; i < 10; i++) {
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(1000));  // Every 1 second
        
        safe_printf("\n=== Performance Monitor (Second %d) ===\n", i + 1);
        safe_printf("Core 0 iterations: %lu (avg: %llu μs)\n", 
                   core0_counter, core0_counter > 0 ? core0_total_time / core0_counter : 0);
        safe_printf("Core 1 iterations: %lu (avg: %llu μs)\n", 
                   core1_counter, core1_counter > 0 ? core1_total_time / core1_counter : 0);
        safe_printf("Queue messages waiting: %d\n", uxQueueMessagesWaiting(core_queue));
        safe_printf("Free heap: %d bytes\n", esp_get_free_heap_size());
    }
    
    vTaskDelete(NULL);
}

void app_main() {
    printf("ESP32 Dual-Core Architecture Analysis\n");
    printf("=====================================\n");
    
    // Create synchronization objects
    core_queue = xQueueCreate(10, sizeof(core_message_t));
    print_mutex = xSemaphoreCreateMutex();
    
    if(core_queue == NULL || print_mutex == NULL) {
        printf("Failed to create synchronization objects!\n");
        return;
    }
    
    printf("Creating tasks...\n");
    
    // Create tasks pinned to specific cores
    BaseType_t core0_result = xTaskCreatePinnedToCore(
        core0_task,           // Task function
        "Core0Task",          // Name
        4096,                 // Stack size
        NULL,                 // Parameters
        2,                    // Priority
        NULL,                 // Task handle
        0                     // Core 0 (PRO_CPU)
    );
    
    BaseType_t core1_result = xTaskCreatePinnedToCore(
        core1_task,           // Task function
        "Core1Task",          // Name
        4096,                 // Stack size
        NULL,                 // Parameters
        2,                    // Priority
        NULL,                 // Task handle
        1                     // Core 1 (APP_CPU)
    );
    
    BaseType_t monitor_result = xTaskCreate(
        monitor_task,         // Task function
        "MonitorTask",        // Name
        2048,                 // Stack size
        NULL,                 // Parameters
        1,                    // Priority (lower than worker tasks)
        NULL                  // Task handle
    );
    
    if(core0_result != pdPASS || core1_result != pdPASS || monitor_result != pdPASS) {
        printf("Failed to create tasks!\n");
        return;
    }
    
    printf("Tasks created successfully. Monitoring dual-core performance...\n\n");
    
    // Main task becomes idle
    vTaskDelay(pdMS_TO_TICKS(12000));  // Wait 12 seconds
    
    printf("\n=== Final Results ===\n");
    printf("Core 0 total iterations: %lu\n", core0_counter);
    printf("Core 1 total iterations: %lu\n", core1_counter);
    printf("Core 0 average time per iteration: %llu μs\n", 
           core0_counter > 0 ? core0_total_time / core0_counter : 0);
    printf("Core 1 average time per iteration: %llu μs\n", 
           core1_counter > 0 ? core1_total_time / core1_counter : 0);
    
    printf("\nDual-core analysis complete!\n");
}