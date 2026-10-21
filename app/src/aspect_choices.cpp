#include "aspect_choices.h"

namespace vj {

const AspectChoice kAspectChoices[kAspectChoiceCount] = {
    {"Core default", 0.0f},
    {"4:3", 4.0f / 3.0f},
    {"16:9", 16.0f / 9.0f},
    {"8:7 (square pixels)", 8.0f / 7.0f},
};

}  // namespace vj
