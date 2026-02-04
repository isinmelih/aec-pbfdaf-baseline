#pragma once

struct ApoParams {
    // filterLen: adaptive filter length in samples (128-2048; trade-off CPU vs. cancellation)
    int filterLen;  
    
    // mu: step size (0.001-0.1 recommended; higher -> faster but unstable)
    float mu;       
    
    // epsilon: regularization term (prevents weight drift in low signal conditions)
    float epsilon;  
};

struct AIParams {
    int sampleRate;
    int channels;
};
