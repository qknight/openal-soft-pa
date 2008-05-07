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

/*
** The pulseaudio extension to openal-soft was created using:
**  - parts from the pulseaudio documentation
**  - parts from the gst pulse plugin
**  - parts from other plugins from within this project (alsa backend)
** All code was put together by Joachim Schiele <js@dune2.de>
**
** I personally want to thank Chris Robinson for his insightfully and patient help
** with all the code/questions I bothered him.
**
** if you want to try the pulseaudio backend or if you want to help improving it:
**  git clone http://lastlog.de/git/openal-soft-pulseaudio
**
** Once you compiled the library you can test it without having to install it. 
** 'cd' into the directory containing libopenal.so and then simply do:
**   export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
**   ln -s libopenal.so libopenal.so.0
** And next you should check if your application uses the library with ldd
** For example the game "spring" uses this (Without the export of LD_LIBRARY_PATH):
**  ldd /usr/games/bin/spring | grep -i open
**        libopenal.so.0 => /usr/lib/libopenal.so.0 (0xb7ade000)
**        libGL.so.1 => //usr//lib/opengl/ati/lib/libGL.so.1 (0xb79c7000)
** And after the export it's:
**  ldd /usr/games/bin/spring | grep -i open
**        libopenal.so.0 => ./libopenal.so.0 (0xb7a9e000)
**        libGL.so.1 => //usr//lib/opengl/ati/lib/libGL.so.1 (0xb7987000)
** The last step is to create a openal-soft config file to which uses the pulseaudio backend.
**       See alsoftrc.sample
** Now you can launch you application from this terminal using the local library.
** NOTE: this change is only temporary. To make it permanent copy this library 'over'
**       the library found in /usr/lib/  (see the exact openal path in the first ldd output)
** NOTE: if you want to test ut/ut2004 with pulseaudio you have to replace the 
**       library (for instance) /opt/ut2004/System/openal.so with the one you just built.
**
** once the code is stable and all TODOs are removed it's hopefully merged into openal-soft
*/

#include <stdio.h>
#include <stdlib.h>

#include "config.h"

#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <locale.h>

#include <pulse/pulseaudio.h>

#if PA_API_VERSION < 9
#error Invalid PulseAudio API version
#endif

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
# define ENDIANNESS   "LITTLE_ENDIAN, BIG_ENDIAN"
#else
# define ENDIANNESS   "BIG_ENDIAN, LITTLE_ENDIAN"
#endif

static char *pulseaudio_playback_device;
static pa_context *context = NULL;
static pa_stream *stream = NULL;

static pa_threaded_mainloop* mainloop = NULL;

static char *stream_name = NULL, *client_name = NULL, *pa_device = NULL;

static pa_sample_spec sample_spec = { 0, 0, 0 };
static pa_channel_map channel_map;
static int channel_map_set = 0;

char *server = NULL;

ALCdevice *device_=NULL;

enum {
    ARG_VERSION = 256,
    ARG_STREAM_NAME,
    ARG_VOLUME,
    ARG_CHANNELMAP
};

/*
** how does this plugin work?
**  game <---> openAL instance <-audio stream-> pulseaudio connection <---> pulseaudio sound server
**                  |
**                  \-> this instance opens the pulseaudio backend which then creates a new
**                      threaded mainloop which puts audio buffers into the callback of then
**                      pulseaudio server
** TODO check endian
**
** TODO make pulseaudio detection in CMakeLists.txt better
**
** TODO check with assert for all possible errors
**
** TODO while ongoing 'pulseaudio server restart' the openAL soft library
**      has to lookup for the server in a certain interval (1/s or 0.2/s)
**      and if it's up, get a new context and a new stream -> start playback
**
** TODO docu on multi channel setup
**
** TODO how to write a openAL soft backend tutorial
**
** TODO remove global variables, so that multiple instances of this openAL library
**      can coexist
**
** TODO linked or dlopen'ed lib
**      04:35 <+KittyCat> alsa can either be linked as a required lib, or is opened with dlopen
**      04:35 <+KittyCat> it still uses the shared lib, but it'd be required for openal to load
**
** TODO pulseaudio library detection in CMakeLists.txt isn't good
**
** TODO 05:16 <+KittyCat> UpdateSize is also the total output buffer size to set
**      05:17 <+KittyCat> generally you should also set it to the period size (how many frames it'll mix for
**            each update)

** FIXME timing is not synced with the openAL using program
**
** TODO test what happens if pulseaudio is running but the playback is blocked because of alsa
**      last time that happened here, there was no callback for writing audio to the stream which
**      then looked like broken code in the pulseaudio client
**
** TODO if pulseaudio backend can't get a context we clean up and return AL_FALSE;
**      this will trigger openAL to try the next audio backend in the .alsoftrc config

** TODO on init: return AL_FALSE and try the next backend.
**      once a output stream was created with success:
**        when no pulse server is there anymore, what to do?
**        simply dump all audio, but keep the game going!
**        then try to reconnect once ever 2 seconds -> on success: resume operation
**
**        AL lib: pulseaudio.c:280: context_state_callback
**        context_state_callback PA_CONTEXT_FAILED
**        AL lib: pulseaudio.c:251: stream_state_callback
**        stream_state_callback PA_STREAM_FAILED
**
** TODO multichannel setup, keep streams in synced
**
** TODO keep stream in sync in general, skip some audio in case of a freez
**
** TODO check the prebuffer and attr values for being ok

//   const char *fname;
//   fname = GetConfigValue("pulseaudio", "capture_channels", "2");
//   printf("capture_channels is set to: %s\n", fname);

*/


/* Connection draining complete */
// static void context_drain_complete(pa_context*c, void *userdata) {
//     (void)userdata;
//     printf("context_drain_complete\n");
//     pa_context_disconnect(c);
// }

/* Stream draining complete */
// static void stream_drain_complete(pa_stream*s, int success, void *userdata) {
//     (void)userdata;
//     (void)s;
//
//     printf("stream_drain_complete\n");
//     pa_operation *o;
//
//     if (!success) {
//         fprintf(stderr, "Failed to drain stream: %s\n", pa_strerror(pa_context_errno(context)));
//         exit(1);
//     }
//
//     pa_stream_disconnect(stream);
//     pa_stream_unref(stream);
//     stream = NULL;
//
//     if (!(o = pa_context_drain(context, context_drain_complete, NULL)))
//         pa_context_disconnect(context);
//     else {
//         pa_operation_unref(o);
//     }
// }


/* This is called whenever new data may be written to the stream */
static void stream_write_callback(pa_stream *s, size_t length, void *userdata) {
//WARNING using printf from this function might cause even more underflows
//     printf("stream_write_callback: requesting max.: %i\n", length);
    (void)userdata;
    assert(s && length);

//     size_t k = pa_frame_size(&sample_spec);
    pa_threaded_mainloop_signal(mainloop, 0);
//     printf("stream_write_callback: pa_frame_size: %i  && bytes: %i\n",k,bytes);

    char *WritePtr;
    int WriteCnt;
    void *data;

    data = pa_xmalloc(length);
    memset(data,0,length);

    // size s = how much data can be taken from openAL

    SuspendContext(NULL);
    WritePtr = data;
    WriteCnt = length;
    aluMixData(device_->Context, WritePtr, WriteCnt, device_->Format);
    ProcessContext(NULL);

    // Return the size of a frame with the specific sample type
    pa_stream_write(s, data, length, /*pa_xfree*/NULL, 0, PA_SEEK_RELATIVE);

    // FIXME should px_xfree be NULL or pa_xfree? what is the difference?
    pa_xfree(data);
}

static void stream_latency_update_callback(pa_stream *s, void *userdata) {
// AL_PRINT("stream_latency_update_callback\n");
    (void)userdata;
    assert(s);
    pa_threaded_mainloop_signal(mainloop, 0);
}

void stream_overflow_callback(pa_stream *p, void *userdata) {
    AL_PRINT("stream_overflow_callback\n");
}

void stream_underflow_callback(pa_stream *p, void *userdata) {
    AL_PRINT("stream_underflow_callback\n");
// this might happen if the computer is slow and the prebuffer is not big enough
}

/* This routine is called whenever the stream state changes */
static void stream_state_callback(pa_stream *s, void *userdata) {
//     GstPulseSink *pulsesink = GST_PULSESINK(userdata);
    AL_PRINT("stream_state_callback\n");
    (void)userdata;
    assert(s);
    char* f;
    f="";
    switch (pa_stream_get_state(s)) {

    case PA_STREAM_READY:
        if (f=="") f="PA_STREAM_READY";

    case PA_STREAM_FAILED:
        if (f=="") f="PA_STREAM_FAILED";

    case PA_STREAM_TERMINATED:
        if (f=="") f="PA_STREAM_TERMINATED";
        pa_threaded_mainloop_signal(/*pulsesink->*/mainloop, 0);
        break;

    case PA_STREAM_UNCONNECTED:
        if (f=="") f="PA_STREAM_UNCONNECTED";
    case PA_STREAM_CREATING:
        if (f=="") f="PA_STREAM_CREATING";
        break;
    }
    printf("stream_state_callback %s\n", f);
}

static void context_state_callback(pa_context *c, void *userdata) {
//     GstPulseSink *pulsesink = GST_PULSESINK(userdata);
    AL_PRINT("context_state_callback\n");
    (void)userdata;
    char* f;
    f="";
    assert(c);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        if (f=="") f="PA_CONTEXT_READY";
    case PA_CONTEXT_TERMINATED:
        if (f=="") f="PA_CONTEXT_TERMINATED";
    case PA_CONTEXT_FAILED:
        if (f=="") f="PA_CONTEXT_FAILED";
        pa_threaded_mainloop_signal(mainloop, 0);
        break;

    case PA_CONTEXT_UNCONNECTED:
        if (f=="") f="PA_CONTEXT_UNCONNECTED";
    case PA_CONTEXT_CONNECTING:
        if (f=="") f="PA_CONTEXT_CONNECTING";
    case PA_CONTEXT_AUTHORIZING:
        if (f=="") f="PA_CONTEXT_AUTHORIZING";
    case PA_CONTEXT_SETTING_NAME:
        if (f=="") f="PA_CONTEXT_SETTING_NAME";
        break;
    }
    printf("context_state_callback %s\n", f);
}

/* Show the current latency */
// static void stream_update_timing_callback(pa_stream *s, int success, void *userdata) {
//   pa_usec_t latency, usec;
//   int negative = 0;
//
//   assert(s);
//
//   if (!success ||
//        pa_stream_get_time(s, &usec) < 0 ||
//        pa_stream_get_latency(s, &latency, &negative) < 0) {
//     fprintf(stderr, "Failed to get latency: %s\n", pa_strerror(pa_context_errno(context)));
//     quit(1);
//     return;
//        }
//
//        fprintf(stderr, "Time: %0.3f sec; Latency: %0.0f usec.  \r",
//                (float) usec / 1000000,
//                 (float) latency * (negative?-1:1));
// }

// bool getPulseContext() {

// }

static ALCboolean PulseAudioOpenPlayback(ALCdevice *device, const ALCchar *deviceName) {
    AL_PRINT("alcPulseAudio PulseAudioOpenPlayback\n");

    //FIXME this code is linuxonly and needs to be handled like that
    FILE* input = fopen("/proc/self/cmdline","r");

    if (input != NULL) {
        int ch = getc( input );
        while ( ch != EOF ) {
            if (ch == ' ')
                break;
            printf( "%c", ch );
            ch = getc( input );
        }
        printf("\n");
    }

    device_=device;
    (void)deviceName;

    setlocale(LC_ALL, "");

    switch (aluBytesFromFormat(device->Format)) {
    case 1:
        sample_spec.format = PA_SAMPLE_U8;
        printf("fromat: AFMT_U8\n");
        break;
    case 2:
        sample_spec.format = PA_SAMPLE_S16LE;
        printf("fromat: AFMT_S16_NE\n");
        break;
    default:
        AL_PRINT("fromat: Unknown format?! %x\n", device->Format);
        goto error_and_forced_exit;
    }

    sample_spec.rate = device->Frequency;
    sample_spec.channels = aluChannelsFromFormat(device->Format);

    assert(pa_sample_spec_valid(&sample_spec));

    char t[PA_SAMPLE_SPEC_SNPRINT_MAX];
    pa_sample_spec_snprint(t, sizeof(t), &sample_spec);
    fprintf(stderr, "Using sample spec '%s'\n", t);

    pa_channel_map_init_auto(&channel_map, sample_spec.channels, PA_CHANNEL_MAP_ALSA);

    if (channel_map_set && channel_map.channels != sample_spec.channels) {
        fprintf(stderr, "Channel map doesn't match file.\n");
        goto error_and_forced_exit;
    }

    const char *n = "openAL-soft client";

    if (!client_name) {
        client_name = pa_locale_to_utf8(n);
        if (!client_name)
            client_name = pa_utf8_filter(n);
    }

    const char *n1 = "openAL out";

    stream_name = pa_locale_to_utf8(n1);
    if (!stream_name)
        stream_name = pa_utf8_filter(n1);

    /* Set up a new main loop */
    if (!(mainloop = pa_threaded_mainloop_new())) {
        fprintf(stderr, "pa_threaded_mainloop_new() failed.\n");
        goto error_and_forced_exit;
    }

    // this scope will be outsourced in a function
//     getPulseContext();
    /* Create a new connection context */
    if (!(context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), client_name))) {
      fprintf(stderr, "pa_context_new() failed.\n");
      goto error_and_forced_exit;
    }

    pa_context_set_state_callback(context, context_state_callback, NULL);

    /* Connect the context */
    if (pa_context_connect(context, server, 0, NULL) < 0) {
      printf("AO: [pulse] Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
      goto error_and_forced_exit;
    }

    pa_threaded_mainloop_lock(mainloop);

    if (pa_threaded_mainloop_start(mainloop) < 0) {
      printf("AO: [pulse] Failed to start main loop\n");
      goto error_and_forced_exit;
    }

    /* Wait until the context is ready */
    pa_threaded_mainloop_wait(mainloop);

    if (pa_context_get_state(context) != PA_CONTEXT_READY) {
      printf("AO: [pulse] Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
      goto error_and_forced_exit;
    }

    if (!(stream = pa_stream_new(context, stream_name, &sample_spec, &channel_map))) {
      printf("AO: [pulse] Failed to create stream: %s\n", pa_strerror(pa_context_errno(context)));
      goto error_and_forced_exit;
    }

    pa_stream_set_state_callback(stream, stream_state_callback, NULL);
    pa_stream_set_latency_update_callback(stream, stream_latency_update_callback, NULL);
    pa_stream_set_write_callback(stream, stream_write_callback, NULL);
    pa_stream_set_overflow_callback(stream,stream_overflow_callback, NULL);
    pa_stream_set_underflow_callback(stream,stream_underflow_callback, NULL);

    /*
    some usefull comments about pa_buffer_attr can be found here:
    - /usr/include/pulse/stream.h
    - http://www.mail-archive.com/pulseaudio-discuss%40mail.0pointer.de/msg01012.html
    */

    pa_buffer_attr attr;
    memset(&attr, 0, sizeof(attr));

    // not used right now, can be enabled in pa_stream_connect_playback() if the third argument
    // is &attr instead of NULL
    // this code is obsolete once the glitch_free pulseaudio branche is working
    attr.tlength=8000;
    attr.maxlength=16000;//attr.tlength*2;
    attr.prebuf=8000;
    attr.minreq=0;
    attr.fragsize=0;

    printf("%i maxlength\n", attr.maxlength);
    printf("%i tlength\n", attr.tlength);
    printf("%i prebuf\n", attr.prebuf);
    printf("%i minreq\n", attr.minreq);
    printf("%i fragsize\n", attr.fragsize);

    // latency happens from this call
    if (pa_stream_connect_playback(stream, NULL, NULL/*&attr*/, PA_STREAM_INTERPOLATE_TIMING|PA_STREAM_AUTO_TIMING_UPDATE, NULL, NULL) < 0) {
      printf("AO: [pulse] Failed to connect stream: %s\n", pa_strerror(pa_context_errno(context)));
      goto error_and_forced_exit;
    }
    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait(mainloop);

    if (pa_stream_get_state(stream) != PA_STREAM_READY) {
      printf("AO: [pulse] Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
      goto error_and_forced_exit;
    }

    pa_threaded_mainloop_unlock(mainloop);

    return ALC_TRUE;

error_and_forced_exit:

    printf("error_and_forced_exit\n");

    //FIXME, free stuff which might be in use already
    printf("FIXME, free stuff which might be in use already\n");

    return ALC_FALSE;
}

static void PulseAudioClosePlayback(ALCdevice *device) {
    (void)device;

    AL_PRINT("alcPulseAudio PulseAudioClosePlayback\n");

    pa_threaded_mainloop_lock(mainloop);

    if (stream)
        pa_stream_unref(stream);

    if (context)
        pa_context_unref(context);

    if (mainloop) {
        pa_threaded_mainloop_unlock(mainloop);
        pa_threaded_mainloop_stop(mainloop);
    }

    pa_xfree(server);
    pa_xfree(pa_device);
    pa_xfree(client_name);
    pa_xfree(stream_name);
//     pa_mainloop_free(mainloop);

    printf("alcPulseAudio PulseAudioClosePlayback done\n");
//   pa_threaded_mainloop_unlock(mainloop);
}

static ALCboolean PulseAudioOpenCapture(ALCdevice *pDevice, const ALCchar *deviceName, ALCuint frequency, ALCenum format, ALCsizei SampleSize) {
    AL_PRINT("alcPulseAudio PulseAudioOpenCapture\n");
    AL_PRINT("FIXME: NOT DONE YET\n");
    (void)pDevice;
    (void)deviceName;
    (void)frequency;
    (void)format;
    (void)SampleSize;

    return ALC_TRUE;
}

static void PulseAudioCloseCapture(ALCdevice *pDevice) {
    AL_PRINT("alcPulseAudio PulseAudioCloseCapture\n");
    (void)pDevice;
}

static void PulseAudioStartCapture(ALCdevice *pDevice) {
    AL_PRINT("alcPulseAudio PulseAudioStartCapture\n");
    (void)pDevice;
}

static void PulseAudioStopCapture(ALCdevice *pDevice) {
    AL_PRINT("alcPulseAudio PulseAudioStopCapture\n");
    (void)pDevice;
}

static void PulseAudioCaptureSamples(ALCdevice *pDevice, ALCvoid *pBuffer, ALCuint lSamples) {
    AL_PRINT("alcPulseAudio PulseAudioCaptureSamples\n");
    (void)pDevice;
    (void)pBuffer;
    (void)lSamples;
}

static ALCuint PulseAudioAvailableSamples(ALCdevice *pDevice) {
    AL_PRINT("alcPulseAudio PulseAudioAvailableSamples\n");
    (void)pDevice;
    return ALC_TRUE;
}

BackendFuncs PulseAudioFuncs = {
    PulseAudioOpenPlayback,
    PulseAudioClosePlayback,
    PulseAudioOpenCapture,
    PulseAudioCloseCapture,
    PulseAudioStartCapture,
    PulseAudioStopCapture,
    PulseAudioCaptureSamples,
    PulseAudioAvailableSamples
};

void alcPulseAudioInit(BackendFuncs *FuncList) {
    AL_PRINT("alcPulseAudioInit\n");

    *FuncList = PulseAudioFuncs;

    pulseaudio_playback_device = AppendDeviceList("PulseAudio Playback");
    AppendAllDeviceList(pulseaudio_playback_device);
}
