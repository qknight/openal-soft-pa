/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#define _CRT_SECURE_NO_DEPRECATE // get rid of sprintf security warnings on VS2005

#include "config.h"

#include <math.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#if defined(HAVE_STDINT_H)
#include <stdint.h>
typedef int64_t ALint64;
#elif defined(HAVE___INT64)
typedef __int64 ALint64;
#elif (SIZEOF_LONG == 8)
typedef long ALint64;
#elif (SIZEOF_LONG_LONG == 8)
typedef long long ALint64;
#endif

#ifdef HAVE_SQRTF
#define aluSqrt(x) ((ALfloat)sqrtf((float)(x)))
#else
#define aluSqrt(x) ((ALfloat)sqrt((double)(x)))
#endif

// fixes for mingw32.
#if defined(max) && !defined(__max)
#define __max max
#endif
#if defined(min) && !defined(__min)
#define __min min
#endif

__inline ALuint aluBytesFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_STEREO8:
        case AL_FORMAT_QUAD8:
            return 1;

        case AL_FORMAT_MONO16:
        case AL_FORMAT_STEREO16:
        case AL_FORMAT_QUAD16:
            return 2;

        default:
            return 0;
    }
}

__inline ALuint aluChannelsFromFormat(ALenum format)
{
    switch(format)
    {
        case AL_FORMAT_MONO8:
        case AL_FORMAT_MONO16:
            return 1;

        case AL_FORMAT_STEREO8:
        case AL_FORMAT_STEREO16:
            return 2;

        case AL_FORMAT_QUAD8:
        case AL_FORMAT_QUAD16:
            return 4;

        default:
            return 0;
    }
}

static __inline ALint aluF2L(ALfloat Value)
{
    if(sizeof(ALint) == 4 && sizeof(double) == 8)
    {
        double temp;
        temp = Value + (((65536.0*65536.0*16.0)+(65536.0*65536.0*8.0))*65536.0);
        return *((ALint*)&temp);
    }
    return (ALint)Value;
}

static __inline ALshort aluF2S(ALfloat Value)
{
    ALint i;

    i = aluF2L(Value);
    i = __min( 32767, i);
    i = __max(-32768, i);
    return ((ALshort)i);
}

static __inline ALvoid aluCrossproduct(ALfloat *inVector1,ALfloat *inVector2,ALfloat *outVector)
{
    outVector[0] = inVector1[1]*inVector2[2] - inVector1[2]*inVector2[1];
    outVector[1] = inVector1[2]*inVector2[0] - inVector1[0]*inVector2[2];
    outVector[2] = inVector1[0]*inVector2[1] - inVector1[1]*inVector2[0];
}

static __inline ALfloat aluDotproduct(ALfloat *inVector1,ALfloat *inVector2)
{
    return inVector1[0]*inVector2[0] + inVector1[1]*inVector2[1] +
           inVector1[2]*inVector2[2];
}

static __inline ALvoid aluNormalize(ALfloat *inVector)
{
    ALfloat length, inverse_length;

    length = (ALfloat)aluSqrt(aluDotproduct(inVector, inVector));
    if(length != 0)
    {
        inverse_length = 1.0f/length;
        inVector[0] *= inverse_length;
        inVector[1] *= inverse_length;
        inVector[2] *= inverse_length;
    }
}

static __inline ALvoid aluMatrixVector(ALfloat *vector,ALfloat matrix[3][3])
{
    ALfloat result[3];

    result[0] = vector[0]*matrix[0][0] + vector[1]*matrix[1][0] + vector[2]*matrix[2][0];
    result[1] = vector[0]*matrix[0][1] + vector[1]*matrix[1][1] + vector[2]*matrix[2][1];
    result[2] = vector[0]*matrix[0][2] + vector[1]*matrix[1][2] + vector[2]*matrix[2][2];
    memcpy(vector, result, sizeof(result));
}

static ALvoid CalcSourceParams(ALCcontext *ALContext, ALsource *ALSource,
                               ALenum isMono, ALenum OutputFormat,
                               ALfloat *drysend, ALfloat *wetsend,
                               ALfloat *pitch)
{
    ALfloat ListenerOrientation[6],ListenerPosition[3],ListenerVelocity[3];
    ALfloat InnerAngle,OuterAngle,OuterGain,Angle,Distance,DryMix,WetMix;
    ALfloat Direction[3],Position[3],Velocity[3],SourceToListener[3];
    ALfloat MinVolume,MaxVolume,MinDist,MaxDist,Rolloff;
    ALfloat Pitch,ConeVolume,SourceVolume,PanningFB,PanningLR,ListenerGain;
    ALfloat U[3],V[3],N[3];
    ALfloat DopplerFactor, DopplerVelocity, flSpeedOfSound, flMaxVelocity;
    ALfloat flVSS, flVLS;
    ALint DistanceModel;
    ALfloat Matrix[3][3];
    ALint HeadRelative;
    ALfloat flAttenuation;

    //Get context properties
    DopplerFactor   = ALContext->DopplerFactor;
    DistanceModel   = ALContext->DistanceModel;
    DopplerVelocity = ALContext->DopplerVelocity;
    flSpeedOfSound  = ALContext->flSpeedOfSound;

    //Get listener properties
    ListenerGain = ALContext->Listener.Gain;
    memcpy(ListenerPosition, ALContext->Listener.Position, sizeof(ALContext->Listener.Position));
    memcpy(ListenerVelocity, ALContext->Listener.Velocity, sizeof(ALContext->Listener.Velocity));
    memcpy(&ListenerOrientation[0], ALContext->Listener.Forward, sizeof(ALContext->Listener.Forward));
    memcpy(&ListenerOrientation[3], ALContext->Listener.Up, sizeof(ALContext->Listener.Up));

    //Get source properties
    Pitch        = ALSource->flPitch;
    SourceVolume = ALSource->flGain;
    memcpy(Position,  ALSource->vPosition,    sizeof(ALSource->vPosition));
    memcpy(Velocity,  ALSource->vVelocity,    sizeof(ALSource->vVelocity));
    memcpy(Direction, ALSource->vOrientation, sizeof(ALSource->vOrientation));
    MinVolume    = ALSource->flMinGain;
    MaxVolume    = ALSource->flMaxGain;
    MinDist      = ALSource->flRefDistance;
    MaxDist      = ALSource->flMaxDistance;
    Rolloff      = ALSource->flRollOffFactor;
    OuterGain    = ALSource->flOuterGain;
    InnerAngle   = ALSource->flInnerAngle;
    OuterAngle   = ALSource->flOuterAngle;
    HeadRelative = ALSource->bHeadRelative;

    //Set working variables
    DryMix = (ALfloat)(1.0f);
    WetMix = (ALfloat)(0.0f);

    //Only apply 3D calculations for mono buffers
    if(isMono != AL_FALSE)
    {
        //1. Translate Listener to origin (convert to head relative)
        if(HeadRelative==AL_FALSE)
        {
            Position[0] -= ListenerPosition[0];
            Position[1] -= ListenerPosition[1];
            Position[2] -= ListenerPosition[2];
        }

        //2. Calculate distance attenuation
        Distance = aluSqrt(aluDotproduct(Position, Position));

        flAttenuation = 1.0f;
        switch (DistanceModel)
        {
            case AL_INVERSE_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_INVERSE_DISTANCE:
                if (MinDist > 0.0f)
                {
                    if ((MinDist + (Rolloff * (Distance - MinDist))) > 0.0f)
                        flAttenuation = MinDist / (MinDist + (Rolloff * (Distance - MinDist)));
                }
                break;

            case AL_LINEAR_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_LINEAR_DISTANCE:
                Distance=__min(Distance,MaxDist);
                if (MaxDist != MinDist)
                    flAttenuation = 1.0f - (Rolloff*(Distance-MinDist)/(MaxDist - MinDist));
                break;

            case AL_EXPONENT_DISTANCE_CLAMPED:
                Distance=__max(Distance,MinDist);
                Distance=__min(Distance,MaxDist);
                if (MaxDist < MinDist)
                    break;
                //fall-through
            case AL_EXPONENT_DISTANCE:
                if ((Distance > 0.0f) && (MinDist > 0.0f))
                    flAttenuation = (ALfloat)pow(Distance/MinDist, -Rolloff);
                break;

            case AL_NONE:
            default:
                flAttenuation = 1.0f;
                break;
        }

        // Source Gain + Attenuation
        DryMix = SourceVolume * flAttenuation;

        // Clamp to Min/Max Gain
        DryMix = __min(DryMix,MaxVolume);
        DryMix = __max(DryMix,MinVolume);
        WetMix = __min(WetMix,MaxVolume);
        WetMix = __max(WetMix,MinVolume);
        //3. Apply directional soundcones
        SourceToListener[0] = -Position[0];
        SourceToListener[1] = -Position[1];
        SourceToListener[2] = -Position[2];
        aluNormalize(Direction);
        aluNormalize(SourceToListener);
        Angle = (ALfloat)(180.0*acos(aluDotproduct(Direction,SourceToListener))/3.141592654f);
        if(Angle >= InnerAngle && Angle <= OuterAngle)
            ConeVolume = (1.0f+(OuterGain-1.0f)*(Angle-InnerAngle)/(OuterAngle-InnerAngle));
        else if(Angle > OuterAngle)
            ConeVolume = (1.0f+(OuterGain-1.0f)                                           );
        else
            ConeVolume = 1.0f;

        //4. Calculate Velocity
        if(DopplerFactor != 0.0f)
        {
            flVLS = aluDotproduct(ListenerVelocity, SourceToListener);
            flVSS = aluDotproduct(Velocity, SourceToListener);

            flMaxVelocity = (DopplerVelocity * flSpeedOfSound) / DopplerFactor;

            if (flVSS >= flMaxVelocity)
                flVSS = (flMaxVelocity - 1.0f);
            else if (flVSS <= -flMaxVelocity)
                flVSS = -flMaxVelocity + 1.0f;

            if (flVLS >= flMaxVelocity)
                flVLS = (flMaxVelocity - 1.0f);
            else if (flVLS <= -flMaxVelocity)
                flVLS = -flMaxVelocity + 1.0f;

            pitch[0] = Pitch * ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVLS)) /
                               ((flSpeedOfSound * DopplerVelocity) - (DopplerFactor * flVSS));
        }
        else
            pitch[0] = Pitch;

        //5. Align coordinate system axes
        aluCrossproduct(&ListenerOrientation[0], &ListenerOrientation[3], U); // Right-vector
        aluNormalize(U);                                // Normalized Right-vector
        memcpy(V, &ListenerOrientation[3], sizeof(V));  // Up-vector
        aluNormalize(V);                                // Normalized Up-vector
        memcpy(N, &ListenerOrientation[0], sizeof(N));  // At-vector
        aluNormalize(N);                                // Normalized At-vector
        Matrix[0][0] = U[0]; Matrix[0][1] = V[0]; Matrix[0][2] = -N[0];
        Matrix[1][0] = U[1]; Matrix[1][1] = V[1]; Matrix[1][2] = -N[1];
        Matrix[2][0] = U[2]; Matrix[2][1] = V[2]; Matrix[2][2] = -N[2];
        aluMatrixVector(Position, Matrix);

        //6. Convert normalized position into left/right front/back pannings
        if(Distance != 0.0f)
        {
            aluNormalize(Position);
            PanningLR = 0.5f + 0.5f*Position[0];
            PanningFB = 0.5f + 0.5f*Position[2];
        }
        else
        {
            PanningLR = 0.5f;
            PanningFB = 0.5f;
        }

        //7. Convert pannings into channel volumes
        switch(OutputFormat)
        {
            case AL_FORMAT_MONO8:
            case AL_FORMAT_MONO16:
                drysend[0] = ConeVolume * ListenerGain * DryMix * aluSqrt(1.0f); //Direct
                drysend[1] = ConeVolume * ListenerGain * DryMix * aluSqrt(1.0f); //Direct
                wetsend[0] =              ListenerGain * WetMix * aluSqrt(1.0f); //Room
                wetsend[1] =              ListenerGain * WetMix * aluSqrt(1.0f); //Room
                break;
            case AL_FORMAT_STEREO8:
            case AL_FORMAT_STEREO16:
                drysend[0] = ConeVolume * ListenerGain * DryMix * aluSqrt(1.0f-PanningLR); //L Direct
                drysend[1] = ConeVolume * ListenerGain * DryMix * aluSqrt(     PanningLR); //R Direct
                wetsend[0] =              ListenerGain * WetMix * aluSqrt(1.0f-PanningLR); //L Room
                wetsend[1] =              ListenerGain * WetMix * aluSqrt(     PanningLR); //R Room
                break;
            case AL_FORMAT_QUAD8:
            case AL_FORMAT_QUAD16:
                drysend[0] = ConeVolume * ListenerGain * DryMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB)); //FL Direct
                drysend[1] = ConeVolume * ListenerGain * DryMix * aluSqrt((     PanningLR)*(1.0f-PanningFB)); //FR Direct
                drysend[2] = ConeVolume * ListenerGain * DryMix * aluSqrt((1.0f-PanningLR)*(     PanningFB)); //BL Direct
                drysend[3] = ConeVolume * ListenerGain * DryMix * aluSqrt((     PanningLR)*(     PanningFB)); //BR Direct
                wetsend[0] =              ListenerGain * WetMix * aluSqrt((1.0f-PanningLR)*(1.0f-PanningFB)); //FL Room
                wetsend[1] =              ListenerGain * WetMix * aluSqrt((     PanningLR)*(1.0f-PanningFB)); //FR Room
                wetsend[2] =              ListenerGain * WetMix * aluSqrt((1.0f-PanningLR)*(     PanningFB)); //BL Room
                wetsend[3] =              ListenerGain * WetMix * aluSqrt((     PanningLR)*(     PanningFB)); //BR Room
                break;
            default:
                break;
        }
    }
    else
    {
        //1. Multi-channel buffers always play "normal"
        drysend[0] = SourceVolume * 1.0f * ListenerGain;
        drysend[1] = SourceVolume * 1.0f * ListenerGain;
        drysend[2] = SourceVolume * 1.0f * ListenerGain;
        drysend[3] = SourceVolume * 1.0f * ListenerGain;
        wetsend[0] = SourceVolume * 0.0f * ListenerGain;
        wetsend[1] = SourceVolume * 0.0f * ListenerGain;
        wetsend[2] = SourceVolume * 0.0f * ListenerGain;
        wetsend[3] = SourceVolume * 0.0f * ListenerGain;

        pitch[0] = Pitch;
    }
}

ALvoid aluMixData(ALCcontext *ALContext,ALvoid *buffer,ALsizei size,ALenum format)
{
    static float DryBuffer[BUFFERSIZE][OUTPUTCHANNELS];
    static float WetBuffer[BUFFERSIZE][OUTPUTCHANNELS];
    ALfloat DrySend[OUTPUTCHANNELS] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ALfloat WetSend[OUTPUTCHANNELS] = { 0.0f, 0.0f, 0.0f, 0.0f };
    ALuint BlockAlign,BufferSize;
    ALuint DataSize=0,DataPosInt=0,DataPosFrac=0;
    ALuint Channels,Bits,Frequency,ulExtraSamples;
    ALfloat Pitch;
    ALint Looping,increment,State;
    ALuint Buffer,fraction;
    ALuint SamplesToDo;
    ALsource *ALSource;
    ALbuffer *ALBuffer;
    ALfloat value;
    ALshort *Data;
    ALuint i,j,k;
    ALbufferlistitem *BufferListItem;
    ALuint loop;
    ALint64 DataSize64,DataPos64;

    SuspendContext(ALContext);

    if(buffer)
    {
        //Figure output format variables
        BlockAlign  = aluChannelsFromFormat(format);
        BlockAlign *= aluBytesFromFormat(format);

        size /= BlockAlign;
        while(size > 0)
        {
            //Setup variables
            ALSource = (ALContext ? ALContext->Source : NULL);
            SamplesToDo = min(size, BUFFERSIZE);

            //Clear mixing buffer
            memset(DryBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));
            memset(WetBuffer, 0, SamplesToDo*OUTPUTCHANNELS*sizeof(ALfloat));

            //Actual mixing loop
            while(ALSource)
            {
                j = 0;
                State = ALSource->state;
                while(State == AL_PLAYING && j < SamplesToDo)
                {
                    DataSize = 0;
                    DataPosInt = 0;
                    DataPosFrac = 0;

                    //Get buffer info
                    if((Buffer = ALSource->ulBufferID))
                    {
                        ALBuffer = (ALbuffer*)ALTHUNK_LOOKUPENTRY(Buffer);

                        Data      = ALBuffer->data;
                        Bits      = aluBytesFromFormat(ALBuffer->format) * 8;
                        Channels  = aluChannelsFromFormat(ALBuffer->format);
                        DataSize  = ALBuffer->size;
                        Frequency = ALBuffer->frequency;

                        CalcSourceParams(ALContext, ALSource,
                                         (Channels==1) ? AL_TRUE : AL_FALSE,
                                         format, DrySend, WetSend, &Pitch);


                        Pitch = (Pitch*Frequency) / ALContext->Frequency;
                        DataSize = DataSize / (Bits*Channels/8);

                        //Get source info
                        DataPosInt = ALSource->position;
                        DataPosFrac = ALSource->position_fraction;

                        //Compute 18.14 fixed point step
                        increment = aluF2L(Pitch*(1L<<FRACTIONBITS));
                        if(increment > (MAX_PITCH<<FRACTIONBITS))
                            increment = (MAX_PITCH<<FRACTIONBITS);

                        //Figure out how many samples we can mix.
                        //Pitch must be <= 4 (the number below !)
                        DataSize64 = DataSize+MAX_PITCH;
                        DataSize64 <<= FRACTIONBITS;
                        DataPos64 = DataPosInt;
                        DataPos64 <<= FRACTIONBITS;
                        DataPos64 += DataPosFrac;
                        BufferSize = (ALuint)((DataSize64-DataPos64) / increment);
                        BufferListItem = ALSource->queue;
                        for(loop = 0; loop < ALSource->BuffersPlayed; loop++)
                        {
                            if(BufferListItem)
                                BufferListItem = BufferListItem->next;
                        }
                        if (BufferListItem)
                        {
                            if (BufferListItem->next)
                            {
                                if(BufferListItem->next->buffer &&
                                   ((ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer))->data)
                                {
                                    ulExtraSamples = min(((ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer))->size, (ALint)(16*Channels));
                                    memcpy(&Data[DataSize*Channels], ((ALbuffer*)ALTHUNK_LOOKUPENTRY(BufferListItem->next->buffer))->data, ulExtraSamples);
                                }
                            }
                            else if (ALSource->bLooping)
                            {
                                if (ALSource->queue->buffer)
                                {
                                    if(((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer))->data)
                                    {
                                        ulExtraSamples = min(((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer))->size, (ALint)(16*Channels));
                                        memcpy(&Data[DataSize*Channels], ((ALbuffer*)ALTHUNK_LOOKUPENTRY(ALSource->queue->buffer))->data, ulExtraSamples);
                                    }
                                }
                            }
                        }
                        BufferSize = min(BufferSize, (SamplesToDo-j));

                        //Actual sample mixing loop
                        Data += DataPosInt*Channels;
                        while(BufferSize--)
                        {
                            k = DataPosFrac>>FRACTIONBITS;
                            fraction = DataPosFrac&FRACTIONMASK;
                            if(Channels==1)
                            {
                                //First order interpolator
                                value = (ALfloat)((ALshort)(((Data[k]*((1L<<FRACTIONBITS)-fraction))+(Data[k+1]*(fraction)))>>FRACTIONBITS));
                                //Direct path final mix buffer and panning
                                DryBuffer[j][0] += value*DrySend[0];
                                DryBuffer[j][1] += value*DrySend[1];
                                DryBuffer[j][2] += value*DrySend[2];
                                DryBuffer[j][3] += value*DrySend[3];
                                //Room path final mix buffer and panning
                                WetBuffer[j][0] += value*WetSend[0];
                                WetBuffer[j][1] += value*WetSend[1];
                                WetBuffer[j][2] += value*WetSend[2];
                                WetBuffer[j][3] += value*WetSend[3];
                            }
                            else
                            {
                                //First order interpolator (left)
                                value = (ALfloat)((ALshort)(((Data[k*2  ]*((1L<<FRACTIONBITS)-fraction))+(Data[k*2+2]*(fraction)))>>FRACTIONBITS));
                                //Direct path final mix buffer and panning (left)
                                DryBuffer[j][0] += value*DrySend[0];
                                //Room path final mix buffer and panning (left)
                                WetBuffer[j][0] += value*WetSend[0];
                                //First order interpolator (right)
                                value = (ALfloat)((ALshort)(((Data[k*2+1]*((1L<<FRACTIONBITS)-fraction))+(Data[k*2+3]*(fraction)))>>FRACTIONBITS));
                                //Direct path final mix buffer and panning (right)
                                DryBuffer[j][1] += value*DrySend[1];
                                //Room path final mix buffer and panning (right)
                                WetBuffer[j][1] += value*WetSend[1];
                            }
                            DataPosFrac += increment;
                            j++;
                        }
                        DataPosInt += (DataPosFrac>>FRACTIONBITS);
                        DataPosFrac = (DataPosFrac&FRACTIONMASK);

                        //Update source info
                        ALSource->position = DataPosInt;
                        ALSource->position_fraction = DataPosFrac;
                    }

                    //Handle looping sources
                    if(!Buffer || DataPosInt >= DataSize)
                    {
                        //queueing
                        if(ALSource->queue)
                        {
                            Looping = ALSource->bLooping;
                            if(ALSource->BuffersPlayed < (ALSource->BuffersInQueue-1))
                            {
                                BufferListItem = ALSource->queue;
                                for(loop = 0; loop <= ALSource->BuffersPlayed; loop++)
                                {
                                    if(BufferListItem)
                                    {
                                        if(!Looping)
                                            BufferListItem->bufferstate = PROCESSED;
                                        BufferListItem = BufferListItem->next;
                                    }
                                }
                                if(!Looping)
                                    ALSource->BuffersProcessed++;
                                if(BufferListItem)
                                    ALSource->ulBufferID = BufferListItem->buffer;
                                ALSource->position = DataPosInt-DataSize;
                                ALSource->position_fraction = DataPosFrac;
                                ALSource->BuffersPlayed++;
                            }
                            else
                            {
                                if(!Looping)
                                {
                                    /* alSourceStop */
                                    ALSource->state = AL_STOPPED;
                                    ALSource->inuse = AL_FALSE;
                                    ALSource->BuffersPlayed = ALSource->BuffersProcessed = ALSource->BuffersInQueue;
                                    BufferListItem = ALSource->queue;
                                    while(BufferListItem != NULL)
                                    {
                                        BufferListItem->bufferstate = PROCESSED;
                                        BufferListItem = BufferListItem->next;
                                    }
                                }
                                else
                                {
                                    /* alSourceRewind */
                                    /* alSourcePlay */
                                    ALSource->state = AL_PLAYING;
                                    ALSource->inuse = AL_TRUE;
                                    ALSource->play = AL_TRUE;
                                    ALSource->BuffersPlayed = 0;
                                    ALSource->BufferPosition = 0;
                                    ALSource->lBytesPlayed = 0;
                                    ALSource->BuffersProcessed = 0;
                                    BufferListItem = ALSource->queue;
                                    while(BufferListItem != NULL)
                                    {
                                        BufferListItem->bufferstate = PENDING;
                                        BufferListItem = BufferListItem->next;
                                    }
                                    ALSource->ulBufferID = ALSource->queue->buffer;

                                    ALSource->position = DataPosInt-DataSize;
                                    ALSource->position_fraction = DataPosFrac;
                                }
                            }
                        }
                    }

                    //Get source state
                    State = ALSource->state;
                }

                ALSource = ALSource->next;
            }

            //Post processing loop
            switch(format)
            {
                case AL_FORMAT_MONO8:
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        *((ALubyte*)buffer) = (ALubyte)((aluF2S(DryBuffer[i][0]+DryBuffer[i][1]+WetBuffer[i][0]+WetBuffer[i][1])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 1;
                    }
                    break;
                case AL_FORMAT_STEREO8:
                    for(i = 0;i < SamplesToDo*2;i++)
                    {
                        *((ALubyte*)buffer) = (ALubyte)((aluF2S(DryBuffer[i>>1][i&1]+WetBuffer[i>>1][i&1])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 1;
                    }
                    break;
                case AL_FORMAT_QUAD8:
                    for(i = 0;i < SamplesToDo*4;i++)
                    {
                        *((ALubyte*)buffer) = (ALubyte)((aluF2S(DryBuffer[i>>2][i&3]+WetBuffer[i>>2][i&3])>>8)+128);
                        buffer = ((ALubyte*)buffer) + 1;
                    }
                    break;
                case AL_FORMAT_MONO16:
                    for(i = 0;i < SamplesToDo;i++)
                    {
                        *((ALshort*)buffer) = aluF2S(DryBuffer[i][0]+DryBuffer[i][1]+WetBuffer[i][0]+WetBuffer[i][1]);
                        buffer = ((ALshort*)buffer) + 1;
                    }
                    break;
                case AL_FORMAT_STEREO16:
                default:
                    for(i = 0;i < SamplesToDo*2;i++)
                    {
                        *((ALshort*)buffer) = aluF2S(DryBuffer[i>>1][i&1]+WetBuffer[i>>1][i&1]);
                        buffer = ((ALshort*)buffer) + 1;
                    }
                    break;
                case AL_FORMAT_QUAD16:
                    for(i = 0;i < SamplesToDo*4;i++)
                    {
                        *((ALshort*)buffer) = aluF2S(DryBuffer[i>>2][i&3]+WetBuffer[i>>2][i&3]);
                        buffer = ((ALshort*)buffer) + 1;
                    }
                    break;
            }

            size -= SamplesToDo;
        }
    }

    ProcessContext(ALContext);
}