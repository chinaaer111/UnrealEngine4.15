// This file was @generated with LibOVRPlatform/codegen/main. Do not modify it!

#ifndef OVR_MICROPHONE_H
#define OVR_MICROPHONE_H

#include "OVR_Platform_Defs.h"
#include "OVR_Types.h"
#include <stdint.h>
#include <stddef.h>

typedef struct ovrMicrophone *ovrMicrophoneHandle;

/// Returns the size of the internal ringbuffer used by the microhone in
/// elements. This size is the maximum number of elements that can ever be
/// returned by ovr_Microphone_GetPCM*.
/// 
/// This function can be safely called from any thread.
OVRP_PUBLIC_FUNCTION(size_t) ovr_Microphone_GetOutputBufferMaxSize(const ovrMicrophoneHandle obj);

/// Gets all available samples of microphone data and copies it into
/// outputBuffer. The microphone will generate data at roughly the rate of 480
/// samples per 10ms. The data format is 16 bit fixed point 48khz mono.
/// 
/// This function can be safely called from any thread.
OVRP_PUBLIC_FUNCTION(size_t) ovr_Microphone_GetPCM(const ovrMicrophoneHandle obj, int16_t *outputBuffer, size_t outputBufferNumElements);

/// Gets all available samples of microphone data and copies it into
/// outputBuffer. The microphone will generate data at roughly the rate of 480
/// samples per 10ms. The data format is 32 bit floating point 48khz mono.
/// 
/// This function can be safely called from any thread.
OVRP_PUBLIC_FUNCTION(size_t) ovr_Microphone_GetPCMFloat(const ovrMicrophoneHandle obj, float *outputBuffer, size_t outputBufferNumElements);

/// DEPRECATED: Use ovr_Microphone_GetPCMFloat instead.
/// 
/// Gets all available samples of microphone data and copies it into
/// outputBuffer. The microphone will generate data at roughly the rate of 480
/// samples per 10ms. The data format is 32 bit floating point 48khz mono.
/// 
/// This function can be safely called from any thread.
OVRP_PUBLIC_FUNCTION(size_t) ovr_Microphone_ReadData(const ovrMicrophoneHandle obj, float *outputBuffer, size_t outputBufferSize);

/// Register a callback that will be called whenever audio data is available
/// for the microphone.
OVRP_PUBLIC_FUNCTION(void) ovr_Microphone_SetAudioDataAvailableCallback(const ovrMicrophoneHandle obj, MicrophoneDataAvailableCallback cb, void *userData);

/// Starts microphone recording. After this is called pcm data can be extracted
/// using ovr_Microphone_GetPCM.
/// 
/// This function can be safely called from any thread.
OVRP_PUBLIC_FUNCTION(void) ovr_Microphone_Start(const ovrMicrophoneHandle obj);

/// Stops microphone recording.
/// 
/// This function can be safely called from any thread.
OVRP_PUBLIC_FUNCTION(void) ovr_Microphone_Stop(const ovrMicrophoneHandle obj);


#endif
