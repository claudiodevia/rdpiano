#include <stdlib.h>

#include <memory>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <portmidi.h>

#include "mame_utils.h"
#include "mcu.h"
#include "sound_chip.h"

static int audio_buffer_size;
static int audio_page_size;

static SDL_AudioDeviceID sdl_audio;

// Global porque el callback de audio de SDL sólo recibe un puntero de usuario y
// el resto del archivo es el original: lo que cambia es que ya no se puede
// escapar sin liberarlo por uno de los `return 2` de abajo.
std::unique_ptr<Mcu> mcu;

void audio_callback(void * /*userdata*/, Uint8 *stream, int len)
{
    const int frames = len / 4;

    for (int i = 0; i < frames; i++)
    {
        // generate_next_sample() devuelve s32 y se sale de ±32767 con un acorde
        // de tres notas: truncar a s16 da wraparound (chasquidos duros) en vez
        // de recorte. El plugin escala en coma flotante; aquí basta con acotar.
        s32 sample = mcu->generate_next_sample();
        if (sample > 32767)
            sample = 32767;
        else if (sample < -32768)
            sample = -32768;

        ((int16_t *)stream)[i * 2] = (int16_t)sample;
        ((int16_t *)stream)[i * 2 + 1] = (int16_t)sample;
    }
}

static const char *audio_format_to_str(int format)
{
    switch (format)
    {
    case AUDIO_S8:
        return "S8";
    case AUDIO_U8:
        return "U8";
    case AUDIO_S16MSB:
        return "S16MSB";
    case AUDIO_S16LSB:
        return "S16LSB";
    case AUDIO_U16MSB:
        return "U16MSB";
    case AUDIO_U16LSB:
        return "U16LSB";
    case AUDIO_S32MSB:
        return "S32MSB";
    case AUDIO_S32LSB:
        return "S32LSB";
    case AUDIO_F32MSB:
        return "F32MSB";
    case AUDIO_F32LSB:
        return "F32LSB";
    }
    return "UNK";
}

int MCU_OpenAudio(int deviceIndex, int pageSize, int pageNum)
{
    SDL_AudioSpec spec = {};
    SDL_AudioSpec spec_actual = {};

    audio_page_size = (pageSize / 2) * 2; // must be even
    audio_buffer_size = audio_page_size * pageNum;

    spec.format = AUDIO_S16SYS;
    spec.freq = 20000;
    // spec.freq = 32000;
    spec.channels = 2;
    spec.callback = audio_callback;
    spec.samples = audio_page_size / 4;

    int num = SDL_GetNumAudioDevices(0);
    if (num == 0)
    {
        printf("No audio output device found.\n");
        return 0;
    }

    if (deviceIndex < -1 || deviceIndex >= num)
    {
        printf("Out of range audio device index is requested. Default audio output "
               "device is selected.\n");
        deviceIndex = -1;
    }

    const char *audioDevicename = deviceIndex == -1 ? "Default device" : SDL_GetAudioDeviceName(deviceIndex, 0);

    sdl_audio = SDL_OpenAudioDevice(deviceIndex == -1 ? NULL : audioDevicename, 0, &spec, &spec_actual, 0);
    if (!sdl_audio)
    {
        return 0;
    }

    printf("Audio device: %s\n", audioDevicename);

    printf("Audio Requested: F=%s, C=%d, R=%d, B=%d\n", audio_format_to_str(spec.format), spec.channels, spec.freq,
           spec.samples);

    printf("Audio Actual: F=%s, C=%d, R=%d, B=%d\n", audio_format_to_str(spec_actual.format), spec_actual.channels,
           spec_actual.freq, spec_actual.samples);
    fflush(stdout);

    SDL_PauseAudioDevice(sdl_audio, 0);

    return 1;
}

// SDL_CloseAudio() es la API antigua y NO cierra un dispositivo abierto con
// SDL_OpenAudioDevice: el callback seguía vivo mientras se destruía el `mcu` del
// final (use-after-free).
void MCU_CloseAudio(void)
{
    if (sdl_audio)
    {
        SDL_CloseAudioDevice(sdl_audio);
        sdl_audio = 0;
    }
}

static PmStream *midiInStream;

int MIDI_Init()
{
    if (Pm_Initialize() != pmNoError)
        return 0;

    int in_id = Pm_CreateVirtualInput("RdPiano", NULL, NULL);
    if (in_id < 0)
        return 0;

    // Sin comprobar esto, midiInStream se queda nulo y MIDI_Update() lo usa.
    if (Pm_OpenInput(&midiInStream, in_id, NULL, 0, NULL, NULL) != pmNoError || midiInStream == NULL)
    {
        midiInStream = NULL;
        return 0;
    }

    Pm_SetFilter(midiInStream, PM_FILT_ACTIVE | PM_FILT_CLOCK | PM_FILT_SYSEX);

    // Empty the buffer, just in case anything got through
    PmEvent receiveBuffer[1];
    while (Pm_Poll(midiInStream) > 0)
    {
        if (Pm_Read(midiInStream, receiveBuffer, 1) < 0)
            break;
    }

    return 1;
}

void MIDI_Quit()
{
    if (midiInStream != NULL)
    {
        Pm_Close(midiInStream);
        midiInStream = NULL;
    }
    Pm_Terminate();
}

void MIDI_Update()
{
    if (midiInStream == NULL)
        return;

    PmEvent event;
    // `> 0`: Pm_Read devuelve negativo en error, y un negativo es cierto -> el
    // bucle no terminaba nunca ante un fallo del dispositivo.
    while (Pm_Read(midiInStream, &event, 1) > 0)
    {
        // sendMidiCmd() empuja en la misma cola que consume el callback de
        // audio desde otro hilo. Bloquear el dispositivo es lo que ofrece SDL
        // para serializarlo; el plugin usa el cerrojo del motor.
        SDL_LockAudioDevice(sdl_audio);
        mcu->sendMidiCmd(Pm_MessageStatus(event.message), Pm_MessageData1(event.message),
                         Pm_MessageData2(event.message));
        SDL_UnlockAudioDevice(sdl_audio);

        printf("MIDI: %02X %02X %02X\n", Pm_MessageStatus(event.message), Pm_MessageData1(event.message),
               Pm_MessageData2(event.message));
    }
}

void load_rom(u8 *data, size_t len, const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (f == NULL)
    {
        printf("Error opening %s\n", filename);
        exit(2);
    }
    size_t read = fread(data, 1, len, f);
    fclose(f);

    if (read != len)
    {
        printf("Error reading %s: %zu de %zu bytes\n", filename, read, len);
        exit(2);
    }
}

int main()
{
    // Medio mega de ROM: estático, no en la pila del hilo principal.
    static u8 temp_ic5[0x20000];
    static u8 temp_ic6[0x20000];
    static u8 temp_ic7[0x20000];
    static u8 temp_progrom[0x2000];
    static u8 temp_paramsrom[0x20000];

    load_rom(temp_ic5, sizeof temp_ic5, "mks20_15179738.BIN");
    load_rom(temp_ic6, sizeof temp_ic6, "mks20_15179737.BIN");
    load_rom(temp_ic7, sizeof temp_ic7, "mks20_15179736.BIN");
    load_rom(temp_progrom, sizeof temp_progrom, "RD200_B.bin");
    load_rom(temp_paramsrom, sizeof temp_paramsrom, "mks20_15179757.BIN");

    // It's important to send a program change after boot to init the parameters
    mcu = std::make_unique<Mcu>(temp_ic5, temp_ic6, temp_ic7, temp_progrom, temp_paramsrom);
    mcu->sendMidiCmd(0xC0, 0, 0); // program change a parche 0: emite el mismo 0x30

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        fprintf(stderr, "FATAL ERROR: Failed to initialize the SDL2: %s.\n", SDL_GetError());
        fflush(stderr);
        return 2;
    }

    if (!MCU_OpenAudio(-1, 512, 32))
    {
        fprintf(stderr, "FATAL ERROR: Failed to open the audio stream.\n");
        fflush(stderr);
        return 2;
    }

    if (!MIDI_Init())
    {
        fprintf(stderr, "ERROR: Failed to initialize the MIDI Input.\nWARNING: "
                        "Continuing without MIDI Input...\n");
        fflush(stderr);
    }

    bool quit_requested = false;
    while (!quit_requested)
    {
        // Sin esperar, el bucle gira al 100 % de un núcleo y le quita CPU al
        // hilo de audio.
        SDL_Delay(1);

        MIDI_Update();

        SDL_Event sdl_event;
        while (SDL_PollEvent(&sdl_event))
        {
            switch (sdl_event.type)
            {
            case SDL_QUIT:
                quit_requested = true;
                break;
            }
        }
    }

    MCU_CloseAudio();
    MIDI_Quit();
    SDL_Quit();

    mcu.reset();

    return 0;
}
