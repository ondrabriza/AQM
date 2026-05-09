
#include "aqm_config.h"
#include <stdint.h>

void push_to_buffer(ring_buffer_t *buf, float value) {
    buf->values[buf->head] = value;
    buf->head = (buf->head + 1) % BUFFER_SIZE;
    if (buf->count < BUFFER_SIZE) buf->count++;
}

float get_recent_average(ring_buffer_t *buf, uint16_t samples) {
    if (buf->count < samples) return -1.0f; // Nedostatek dat

    float sum = 0.0f;
    
    for (uint16_t i = 1; i <= samples; i++) {

        uint16_t idx = (buf->head - i + BUFFER_SIZE) % BUFFER_SIZE;
        sum += buf->values[idx];
    }
    return sum / (float)samples;
}