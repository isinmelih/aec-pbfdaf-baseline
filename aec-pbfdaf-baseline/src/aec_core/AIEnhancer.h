#pragma once
#include <vector>
#include "../../APO/ApoParams.h"
class AIEnhancer {
public:
    AIEnhancer();
    void initialize(const AIParams& p);
    void process(float* inout, size_t frames);
private:
    AIParams params;
    std::vector<float> window;
    std::vector<float> noise;
    std::vector<float> ola;
    std::vector<float> acc;
    size_t accIndex;
    bool initialized;
};
