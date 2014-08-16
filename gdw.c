#include <stdio.h>
#include <CoreServices/CoreServices.h>
#include <CoreAudio/CoreAudio.h>

#define	CA_BUFF_MAX		(1024)
#define	SAMPLE_RATE		(44100)

//#define	SHOW_BUFF_COUNTS	1

int CAStartPlayback(int channels, long buffFrames);
int CAStopPlayback();
int CAPlayBuffer(float *buffer, long buffSamples);
int alDebugMessage(char *s, long code);
static OSStatus appOutputIOProc (AudioDeviceID  inDevice, const AudioTimeStamp*  inNow, const AudioBufferList*   inInputData,
        const AudioTimeStamp*  inInputTime, AudioBufferList*  outOutputData, const AudioTimeStamp* inOutputTime,
        void* defptr);

int main (int argc, const char * argv[])
{
    float	buff[CA_BUFF_MAX], f;
    long	index=0;

    if (!CAStartPlayback(2, CA_BUFF_MAX))
    {
        fprintf(stderr, "Could not start Core Audio playback.\n");
        return(1);
    }

    do
    {
//        if (scanf("%f", &f)!=1)
//            break;
        if (fread(&f, sizeof(float), 1, stdin)!=1)
            break;

        buff[index++] = f;

        if (index==CA_BUFF_MAX)
        {
            if (!CAPlayBuffer(buff, index))
            {
                fprintf(stderr, "Could not play Core Audio buffer.\n");
                return(2);
            }

            index=0;
        }
    }
    while(1);

    CAStopPlayback();

    return(0);
}

#define	MAX_PLAY_BUFFS		(4)

static AudioDeviceID		playDevice;				/* the device we play from		*/
static UInt32			playDeviceBufferSize;	// bufferSize returned by kAudioDevicePropertyBufferSize
static AudioStreamBasicDescription	playDeviceFormat;	// info about the default device
static char			playing;
static long			ioLastPlayBufferTime;	/* used to look for buffer jumps	*/
static long			deviceBufferFrames;		/* how many frames per buffer		*/
static int			playDeviceChannels;
static float			*playBuffer[MAX_PLAY_BUFFS];
static volatile int		playBufferCount, playBufferHead, playBufferTail;

int CAStartPlayback(int channels, long buffFrames)
{
    OSStatus			err = kAudioHardwareNoError;

    {
        UInt32				count;

        playDevice = kAudioDeviceUnknown;
        // get the default output playDevice for the HAL
        count = sizeof(playDevice);		// it is required to pass the size of the data to be returned

        if ((err = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice,  &count, (void *) &playDevice))!=kAudioHardwareNoError)
        {
            alDebugMessage("OpenSoundOutput: AudioHardwareGetProperty-kAudioHardwarePropertyDefaultOutputDevice returned error.", err);
            return(0);
        }
        // 2et the buffersize that the default device uses for IO
        count = sizeof(playDeviceBufferSize);	// it is required to pass the size of the data to be returned

        playDeviceBufferSize = buffFrames * sizeof(float) * channels;		/* set to requested size	*/

        if ((err = AudioDeviceSetProperty(playDevice, 0, 0, false, kAudioDevicePropertyBufferSize, count, &playDeviceBufferSize))!=kAudioHardwareNoError)
        {
            alDebugMessage("OpenSoundOutput: AudioDeviceGetProperty-kAudioDevicePropertyBufferSize returned error.", err);
            return(0);
        }

        // get the buffersize that the default device uses for IO
        count = sizeof(playDeviceBufferSize);	// it is required to pass the size of the data to be returned

        if ((err = AudioDeviceGetProperty(playDevice, 0, false, kAudioDevicePropertyBufferSize, &count, &playDeviceBufferSize))!=kAudioHardwareNoError)
        {
            alDebugMessage("OpenSoundOutput: AudioDeviceGetProperty-kAudioDevicePropertyBufferSize returned error.", err);
            return(0);
        }
        // get a description of the data format used by the default device
        count = sizeof(playDeviceFormat);	// it is required to pass the size of the data to be returned

        if ((err = AudioDeviceGetProperty(playDevice, 0, false, kAudioDevicePropertyStreamFormat, &count, &playDeviceFormat))!=kAudioHardwareNoError)
        {
            alDebugMessage("OpenSoundOutput: AudioDeviceGetProperty-kAudioDevicePropertyStreamFormat returned error.", err);
            return(0);
        }

        if (playDeviceFormat.mFormatID != kAudioFormatLinearPCM)
        {
            alDebugMessage("OpenSoundOutput: playDevice is not linear PCM.", 0);
            return(0);
        }

        if (!(playDeviceFormat.mFormatFlags & kLinearPCMFormatFlagIsFloat))
        {
            alDebugMessage("OpenSoundOutput: playDevice is not float.", 0);
            return(0);
        }

        playDeviceFormat.mSampleRate = SAMPLE_RATE;

        count = sizeof(playDeviceFormat);
        err = AudioDeviceSetProperty(playDevice, 0, 0, 0, kAudioDevicePropertyStreamFormat, count, &playDeviceFormat);

        if (err != kAudioHardwareNoError)
        {
            alDebugMessage("OpenSoundOutput: setting sample rate failed.", err);
//                        return(SetStickyError(theStatus));
        }

        deviceBufferFrames = playDeviceBufferSize/(sizeof(float)*channels);
        playDeviceChannels = channels;
#ifdef _DEBUG_VERSION_
		alDebugMessage("OpenSoundOutput: Buffer Size", playDeviceBufferSize);
		alDebugMessage("OpenSoundOutput: SampleRate", playDeviceFormat.mSampleRate);
		alDebugMessage("OpenSoundOutput: FormatFlags", playDeviceFormat.mFormatFlags);
		alDebugMessage("OpenSoundOutput: BytesPerPacket", playDeviceFormat.mBytesPerPacket);
		alDebugMessage("OpenSoundOutput: FramesPerPacket", playDeviceFormat.mFramesPerPacket);
		alDebugMessage("OpenSoundOutput: ChannelsPerFrame", playDeviceFormat.mChannelsPerFrame);
		alDebugMessage("OpenSoundOutput: BytesPerFrame", playDeviceFormat.mBytesPerFrame);
		alDebugMessage("OpenSoundOutput: BitsPerChannel", playDeviceFormat.mBitsPerChannel);
#endif
    }

    {
        int	i;

        for(i=0; i<MAX_PLAY_BUFFS; i++)
        {
            playBuffer[i] = malloc(sizeof(float) * channels * buffFrames);
        }

        playBufferCount = playBufferHead = playBufferTail = 0;
    }

    return(1);
}


int CAPlayBuffer(float *buffer, long buffSamples)
{
    int	i;

    while(playBufferCount==MAX_PLAY_BUFFS)
        ;

    {
        float	*p;

        p = playBuffer[playBufferTail++];

        if (playBufferTail==MAX_PLAY_BUFFS)
            playBufferTail = 0;

        for(i=0; i<buffSamples; i++)
            *p++ = buffer[i];

        playBufferCount++;
#ifdef SHOW_BUFF_COUNTS
        fprintf(stderr, "+%d ", playBufferCount);
#endif
    }

    if (!playing && playBufferCount==2)
    {
        OSStatus			err = kAudioHardwareNoError;

        ioLastPlayBufferTime = -1L;				/* reset	*/
        // setup our device with an IO proc
        if ((err = AudioDeviceAddIOProc(playDevice, appOutputIOProc, NULL))!=kAudioHardwareNoError)
        {
            alDebugMessage("_StartPlayIO: AudioDeviceAddIOProc() failed.", err);
            return(0);
        }
        // start playing sound through the device
        if ((err = AudioDeviceStart(playDevice, appOutputIOProc))!=kAudioHardwareNoError)
        {
            alDebugMessage("_StartPlayIO: AudioDeviceStart() failed.", err);
            return(0);
        }

        playing = 1;
    }

    return(1);
}

int CAStopPlayback()
{
    long	err;

    if (!playing)
    {
        alDebugMessage("_StopPlayIO: was not playing.", 0);
        return(0);
    }

    playing = 0;
    // stop playing sound through the device
    if ((err = AudioDeviceStop(playDevice, appOutputIOProc))!=kAudioHardwareNoError)
    {
        alDebugMessage("_StopPlayIO: AudioDeviceStop() failed.", err);
        return(0);
    }
    // remove the IO proc from the device
    if ((err = AudioDeviceRemoveIOProc(playDevice, appOutputIOProc))!=kAudioHardwareNoError)
    {
        alDebugMessage("_StopPlayIO: AudioDeviceRemoveIOProc() failed.", err);
        return(0);
    }

    return(1);
}


/* +++ app IO Proc for standard output device +++ */
static OSStatus appOutputIOProc (AudioDeviceID  inDevice, const AudioTimeStamp*  inNow, const AudioBufferList*   inInputData,
        const AudioTimeStamp*  inInputTime, AudioBufferList*  outOutputData, const AudioTimeStamp* inOutputTime,
        void* defptr)
{
#pragma unused(inDevice, inNow, inInputData, inInputTime, defptr)
    float		*out = outOutputData->mBuffers[0].mData;
    int		numFrames = playDeviceBufferSize / playDeviceFormat.mBytesPerFrame;
    int		numChannels = playDeviceChannels;
    int		i, j;
    float		*p;

    if (!playing)
    {
        alDebugMessage("appOutputIOProc: called when not playing!!!", 0);
        return kAudioHardwareNoError;
    }

#ifndef _DEBUG_VERSION_
    {
        long	currTime;

        currTime = inOutputTime->mSampleTime;

        if (ioLastPlayBufferTime!=-1)
        {
            if (currTime - ioLastPlayBufferTime != deviceBufferFrames)
            {
                alDebugMessage("appOutputIOProc: underflowed...we skipped.", currTime - ioLastPlayBufferTime);
            }
            else
                ioLastPlayBufferTime = currTime;
        }
        else
            ioLastPlayBufferTime = currTime;
    }
#endif

    if (playBufferCount==0)
    {
        alDebugMessage("appOutputIOProc: underflowed...", 0);
        return kAudioHardwareNoError;
    }

    p = playBuffer[playBufferHead++];

    if (playBufferHead==MAX_PLAY_BUFFS)
        playBufferHead = 0;

    for(i=0; i<numFrames; i++)
    {
        for(j=0; j<numChannels; j++)
        {
            *out++ = *p;				/* do channels	*/
        }

        p++;
    }

    playBufferCount--;
#ifdef SHOW_BUFF_COUNTS
        fprintf(stderr, "-%d ", playBufferCount);
#endif
    return kAudioHardwareNoError;
}



int alDebugMessage(char *s, long code)
{
    fprintf(stderr, "%s (%ld/%lx)\n", s, code, code);
    return(1);
}
