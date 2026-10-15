/**
 * @file standalone.cpp
 * @brief Ejecutable de escucha: el emulador con audio por SDL y MIDI por portmidi.
 *
 * No entra en ctest; es la verificación auditiva de la que habla CLAUDE.md. Arranca el parche 0 a 20 kHz
 * con el mismo boot() que el plugin y abre una entrada MIDI virtual llamada "RdPiano".
 *
 * Uso: rdpiano_standalone [--roms DIR]
 */

#include <stdio.h>

#include <memory>
#include <string>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#include <portmidi.h>

#include "mame_utils.h"
#include "mcu.h"
#include "patches.h"
#include "rom_files.h"

static int audio_buffer_size;
static int audio_page_size;

static SDL_AudioDeviceID sdl_audio;

/// Global porque el callback de audio de SDL solo recibe un puntero de usuario.
std::unique_ptr<Mcu> mcu;

void audio_callback(void * /*userdata*/, Uint8 *stream, int len)
{
    const int frames = len / 4;

    for (int i = 0; i < frames; i++)
    {
        // generate_next_sample() devuelve s32 y se sale de ±32767 con un acorde
        // de tres notas: truncar a s16 da wraparound (chasquidos duros) en vez
        // de recorte. El plugin escala en coma flotante; aquí basta con acotar.
        s32 sample = mcu->generate_next_sample(false);
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

    audio_page_size = (pageSize / 2) * 2; // tiene que ser par
    audio_buffer_size = audio_page_size * pageNum;

    spec.format = AUDIO_S16SYS;
    spec.freq = patchSampleRates[0];
    spec.channels = 2;
    spec.callback = audio_callback;
    spec.samples = audio_page_size / 4;

    int num = SDL_GetNumAudioDevices(0);
    if (num == 0)
    {
        printf("No hay ningún dispositivo de salida de audio.\n");
        return 0;
    }

    if (deviceIndex < -1 || deviceIndex >= num)
    {
        printf("Índice de dispositivo de audio fuera de rango: se usa el de por defecto.\n");
        deviceIndex = -1;
    }

    const char *audioDevicename =
        deviceIndex == -1 ? "Dispositivo por defecto" : SDL_GetAudioDeviceName(deviceIndex, 0);

    sdl_audio = SDL_OpenAudioDevice(deviceIndex == -1 ? NULL : audioDevicename, 0, &spec, &spec_actual, 0);
    if (!sdl_audio)
    {
        return 0;
    }

    printf("Dispositivo de audio: %s\n", audioDevicename);

    printf("Audio pedido: F=%s, C=%d, R=%d, B=%d\n", audio_format_to_str(spec.format), spec.channels, spec.freq,
           spec.samples);

    printf("Audio obtenido: F=%s, C=%d, R=%d, B=%d\n", audio_format_to_str(spec_actual.format), spec_actual.channels,
           spec_actual.freq, spec_actual.samples);
    fflush(stdout);

    SDL_PauseAudioDevice(sdl_audio, 0);

    return 1;
}

/// Cierra con SDL_CloseAudioDevice(): SDL_CloseAudio() no cierra un dispositivo abierto con
/// SDL_OpenAudioDevice() y el callback seguiría vivo al destruir `mcu`.
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

    if (Pm_OpenInput(&midiInStream, in_id, NULL, 0, NULL, NULL) != pmNoError || midiInStream == NULL)
    {
        midiInStream = NULL;
        return 0;
    }

    Pm_SetFilter(midiInStream, PM_FILT_ACTIVE | PM_FILT_CLOCK | PM_FILT_SYSEX);

    // Vacía lo que hubiera entrado antes de abrir.
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
    // Pm_Read devuelve negativo en error.
    while (Pm_Read(midiInStream, &event, 1) > 0)
    {
        // sendMidiCmd() empuja en la cola que consume el callback de audio desde
        // otro hilo: bloquear el dispositivo es como SDL lo serializa.
        SDL_LockAudioDevice(sdl_audio);
        mcu->sendMidiCmd(Pm_MessageStatus(event.message), Pm_MessageData1(event.message),
                         Pm_MessageData2(event.message));
        SDL_UnlockAudioDevice(sdl_audio);

        printf("MIDI: %02X %02X %02X\n", Pm_MessageStatus(event.message), Pm_MessageData1(event.message),
               Pm_MessageData2(event.message));
    }
}

int main(int argc, char **argv)
{
    std::string romsDir = "roms";
    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--roms" && i + 1 < argc)
            romsDir = argv[++i];
        else
        {
            fprintf(stderr, "uso: %s [--roms DIR]\n", argv[0]);
            return 2;
        }
    }

    // Las ROM tienen que sobrevivir al Mcu: viven en la caché hasta el final de main().
    RomCache roms;
    roms.dir = romsDir;
    RdRomSet set;
    const u8 *prog = roms.get(PROG_ROM_FILE, PROG_ROM_SIZE);
    if (!roms.romSet(patchToRomSetId[0], set) || prog == nullptr)
    {
        fprintf(stderr, "ERROR: faltan ROM en %s (mira patches.h para los nombres)\n", romsDir.c_str());
        return 2;
    }

    mcu = std::make_unique<Mcu>(set.ic5, set.ic6, set.ic7, prog, set.ic18);
    mcu->boot(0, false);

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
    {
        fprintf(stderr, "ERROR: no se pudo iniciar SDL2: %s.\n", SDL_GetError());
        fflush(stderr);
        return 2;
    }

    if (!MCU_OpenAudio(-1, 512, 32))
    {
        fprintf(stderr, "ERROR: no se pudo abrir la salida de audio.\n");
        fflush(stderr);
        return 2;
    }

    if (!MIDI_Init())
    {
        fprintf(stderr, "AVISO: no se pudo abrir la entrada MIDI; se sigue sin ella.\n");
        fflush(stderr);
    }

    bool quit_requested = false;
    while (!quit_requested)
    {
        // Sin espera, el bucle ocupa un núcleo entero y le quita CPU al hilo de audio.
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
