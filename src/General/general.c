#include "general.h"

struct Object newObject() {
    struct Object newObj = {
        {0.0, 0.0, 0.0},        // chunk
        {0.0, 0.0, 0.0},        // position
        {0.0, 0.0, 0.0},        // direction
        {0.0, 0.0, 0.0},        // size
        {0.0, 0.0, 0.0},        // center of mass (0 to 1, offset from the middle)        
    };

    return newObj;
}