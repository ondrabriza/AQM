
#include "aqm_config.h"
#include <stdint.h>

void push_to_buffer(ring_buffer_t *buf, float value) {
    buf->values[buf->head] = value;
    buf->head = (buf->head + 1) % BUFFER_SIZE;
    if (buf->count < BUFFER_SIZE) buf->count++;
}

float get_recent_average(ring_buffer_t *buf, uint16_t samples) {
    if (buf->count < samples) return -1.0f;
    
    float sum = 0.0f;
    uint16_t idx = buf->head - 1;
    
    for (uint16_t i = 0; i < samples; i++) {
        if (idx < 0) idx += BUFFER_SIZE;
        sum += buf->values[idx];
        idx--;
    }
    return sum / (float)samples;
}
