add_library(aec_core STATIC
    AECProcessor.cpp
    AECProcessor.h
    AIEnhancer.cpp
    AIEnhancer.h
)

target_include_directories(aec_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
