#include <stdio.h>
#include <CoreAudio/CoreAudio.h>

#define CA_BUFF_MAX  (4096)
#define SAMPLE_RATE  (44100)
#define MAX_PLAY_BUFFS  (4)

static AudioDeviceID  playDevice = kAudioDeviceUnknown;    /* the device we play from  */
static UInt32   playDeviceBufferSize; // bufferSize returned by kAudioDevicePropertyBufferSize
static AudioStreamBasicDescription playDeviceFormat; // info about the default device
static char   playing;
static long   ioLastPlayBufferTime; /* used to look for buffer jumps */
static long   deviceBufferFrames;  /* how many frames per buffer  */
static int   playDeviceChannels;
static float   *playBuffer[MAX_PLAY_BUFFS];
static volatile int  playBufferCount, playBufferHead, playBufferTail;
AudioDeviceIOProcID theIOProcID = NULL;
OSStatus err;


int CAStartPlayback(int channels, long buffFrames) {
    UInt32 propSize;

    // get the default output playDevice for the HAL
    propSize = sizeof(playDevice);
    AudioObjectPropertyAddress propAddr = {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster};
    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &propAddr, 0, NULL, &propSize, &playDevice);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "OpenSoundOutput: AudioHardwareGetProperty-kAudioHardwarePropertyDefaultOutputDevice returned error: %d \n", err);
        return 0;
    }

    // set the buffersize that the default device uses for IO
    propSize = sizeof(playDeviceBufferSize); // it is required to pass the size of the data to be returned
    playDeviceBufferSize = buffFrames * sizeof(float) * channels;  /* set to requested size */
    propAddr.mSelector = kAudioDevicePropertyBufferSize;
    propAddr.mScope = kAudioObjectPropertyScopeOutput;
    err = AudioObjectSetPropertyData(playDevice, &propAddr, 0, NULL, propSize, &playDeviceBufferSize);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "OpenSoundOutput: AudioDeviceSetProperty-kAudioDevicePropertyBufferSize returned error: %d \n", err);
        return 0;
    }
    err = AudioObjectGetPropertyData(playDevice, &propAddr, 0, NULL, &propSize, &playDeviceBufferSize);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "OpenSoundOutput: AudioDeviceGetProperty-kAudioDevicePropertyBufferSize returned error: %d \n", err);
        return 0;
    }

    // get a description of the data format used by the default device
    propSize = sizeof(playDeviceFormat); // it is required to pass the size of the data to be returned
    propAddr.mSelector = kAudioDevicePropertyStreamFormat;
    err = AudioObjectGetPropertyData(playDevice, &propAddr, 0, NULL, &propSize, &playDeviceFormat);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "OpenSoundOutput: AudioDeviceGetProperty-kAudioDevicePropertyStreamFormat returned error: %d \n", err);
        return 0;
    }
    if (playDeviceFormat.mFormatID != kAudioFormatLinearPCM) {
        fprintf(stderr, "OpenSoundOutput: playDevice is not linear PCM.");
        return 0;
    }
    if (!(playDeviceFormat.mFormatFlags & kLinearPCMFormatFlagIsFloat)) {
        fprintf(stderr, "OpenSoundOutput: playDevice is not float.");
        return 0;
    }
    playDeviceFormat.mSampleRate = SAMPLE_RATE;
    err = AudioObjectSetPropertyData(playDevice, &propAddr, 0, NULL, propSize, &playDeviceFormat);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "OpenSoundOutput: setting sample rate failed: %d \n", err);
        return 0;
    }

    deviceBufferFrames = playDeviceBufferSize / (sizeof(float) * channels);
    playDeviceChannels = channels;

    fprintf(stderr, "OpenSoundOutput: Buffer Size = %d\n", playDeviceBufferSize);
    fprintf(stderr, "OpenSoundOutput: SampleRate = %f\n", playDeviceFormat.mSampleRate);
    fprintf(stderr, "OpenSoundOutput: FormatFlags = %d\n", playDeviceFormat.mFormatFlags);
    fprintf(stderr, "OpenSoundOutput: BytesPerPacket = %d\n", playDeviceFormat.mBytesPerPacket);
    fprintf(stderr, "OpenSoundOutput: FramesPerPacket = %d\n", playDeviceFormat.mFramesPerPacket);
    fprintf(stderr, "OpenSoundOutput: ChannelsPerFrame = %d\n", playDeviceFormat.mChannelsPerFrame);
    fprintf(stderr, "OpenSoundOutput: BytesPerFrame = %d\n", playDeviceFormat.mBytesPerFrame);
    fprintf(stderr, "OpenSoundOutput: BitsPerChannel = %d\n", playDeviceFormat.mBitsPerChannel);

    for (int i = 0; i < MAX_PLAY_BUFFS; i++) {
        playBuffer[i] = malloc(sizeof(float) * channels * buffFrames);
    }
    playBufferCount = playBufferHead = playBufferTail = 0;
    return 1;
}


static OSStatus appOutputIOProc (
        AudioDeviceID  inDevice,
        const AudioTimeStamp*  inNow,
        const AudioBufferList*   inInputData,
        const AudioTimeStamp*  inInputTime,
        AudioBufferList*  outOutputData,
        const AudioTimeStamp* inOutputTime,
        void* defptr)
{
    if (!playing) {
        fprintf(stderr, "appOutputIOProc: called when not playing!!!");
        return kAudioHardwareNoError;
    }

    //#ifndef _DEBUG_VERSION_
    //    {
    //        long currTime;
    //        currTime = inOutputTime->mSampleTime;
    //        if (ioLastPlayBufferTime!=-1) {
    //            if (currTime - ioLastPlayBufferTime != deviceBufferFrames) {
    //                fprintf(stderr, "appOutputIOProc: underflowed...we skipped.", currTime - ioLastPlayBufferTime);
    //            } else ioLastPlayBufferTime = currTime;
    //        } else ioLastPlayBufferTime = currTime;
    //    }
    //#endif

    if (playBufferCount==0) {
        fprintf(stderr, "appOutputIOProc: underflowed...");
        return kAudioHardwareNoError;
    }

    int  numFrames = playDeviceBufferSize / playDeviceFormat.mBytesPerFrame;
    float  *out = outOutputData->mBuffers[0].mData;

    float  *p = playBuffer[playBufferHead++];

    if (playBufferHead==MAX_PLAY_BUFFS)
        playBufferHead = 0;

    for(int i=0; i<numFrames; i++) {
        for(int j=0; j<playDeviceChannels; j++) {
            *out++ = *p;    /* do channels */
        }
        p++;
    }

    playBufferCount--;
    fprintf(stderr, "-%d ", playBufferCount);

    return kAudioHardwareNoError;
}



int CAPlayBuffer(float *buffer, long buffSamples)
{

    while (playBufferCount==MAX_PLAY_BUFFS);

    float *p = playBuffer[playBufferTail++];
    if (playBufferTail==MAX_PLAY_BUFFS)
        playBufferTail = 0;
    for (int i=0; i<buffSamples; i++)
        *p++ = buffer[i];
    playBufferCount++;

    fprintf(stderr, "+%d ", playBufferCount);

    if (!playing && playBufferCount == 2) {
        ioLastPlayBufferTime = -1L;    /* reset */

        // setup our device with an IO proc
        err = AudioDeviceCreateIOProcID(playDevice, appOutputIOProc, NULL, &theIOProcID);
        if (err != kAudioHardwareNoError) {
            fprintf(stderr, "_StartPlayIO: AudioDeviceAddIOProc() failed: %d \n", err);
            return 0;
        }
        // start playing sound through the device
        err = AudioDeviceStart(playDevice, theIOProcID);
        if (err != kAudioHardwareNoError) {
            fprintf(stderr, "_StartPlayIO: AudioDeviceStart() failed: %d \n", err);
            return 0;
        }

        playing = 1;
    }

    return 1;
}

int CAStopPlayback()
{
    if (!playing) {
        fprintf(stderr, "_StopPlayIO: was not playing.");
        return 0;
    }

    playing = 0;
    // stop playing sound through the device
    err = AudioDeviceStop(playDevice, theIOProcID);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "_StopPlayIO: AudioDeviceStop() failed: %d \n", err);
        return 0;
    }
    // remove the IO proc from the device
    err = AudioDeviceDestroyIOProcID(playDevice, theIOProcID);
    if (err != kAudioHardwareNoError) {
        fprintf(stderr, "_StopPlayIO: AudioDeviceRemoveIOProc() failed: %d \n", err);
        return 0;
    }
    return 1;
}



int main (int argc, const char * argv[]) {
    float buff[CA_BUFF_MAX], f;
    long index = 0;

    if (!CAStartPlayback(2, CA_BUFF_MAX)) {
        fprintf(stderr, "Could not start Core Audio playback.\n");
        return 1;
    }

    while (!feof(stdin)) {
        if (0) { /////////// slow playback
            int sz = fread(buff, sizeof(float), CA_BUFF_MAX, stdin);
            if (!CAPlayBuffer(buff, sz)) {
                fprintf(stderr, "Could not play Core Audio buffer.\n");
                return 2;
            }
        } else { /////////// fast playback
            fread(&f, sizeof(float), sizeof(float), stdin);
            buff[index++] = f;
            if (index == CA_BUFF_MAX) {
                if (!CAPlayBuffer(buff, index)) {
                    fprintf(stderr, "Could not play Core Audio buffer.\n");
                    return 2;
                }
                index = 0;
            }
        }
    }

    sleep(1); // wait for the last data to play

    CAStopPlayback();

    return 0;
}
