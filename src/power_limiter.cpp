#include "power_limiter.h"
#include "config.h"
#include "logging.h"

namespace PowerLimiter {

    
    //  Stromabschätzung
    
    uint16_t estimateCurrent(const Color::RGB* buffer) {
        uint32_t sum = 0;

        for (uint8_t i = 0; i < MAX_LEDS_PER_RING; i++) {
            sum += buffer[i].r;
            sum += buffer[i].g;
            sum += buffer[i].b;
        }

        // 255*3 = 765 → 60 mA
        return (sum * 60) / 765;
    }


    
    //  Strombegrenzung anwenden
    
    uint16_t applyLimit(Color::RGB* buffer, uint16_t limit_mA) {
        uint16_t current = estimateCurrent(buffer);

        if (current <= limit_mA) {
            return current;
        }

        float scale = (float)limit_mA / (float)current;

        for (uint8_t i = 0; i < MAX_LEDS_PER_RING; i++) {
            buffer[i].r = buffer[i].r * scale;
            buffer[i].g = buffer[i].g * scale;
            buffer[i].b = buffer[i].b * scale;
        }

        LOG_DEBUG(LOG_PWR, F("Strombegrenzung aktiv – Helligkeit reduziert."));

        return limit_mA;
    }

} // namespace PowerLimiter
