#include "configuration.h"

#ifdef M5STACK_CARDPUTER_ADV

#include "AudioBoard.h"

DriverPins PinsAudioBoardES8311;
AudioBoard board(AudioDriverES8311, PinsAudioBoardES8311);

// M5Stack Cardputer-Adv specific init
// Initialises the ES8311 audio codec at 8 kHz / 16-bit for Codec2 voice.
// ES8311 is on I2C bus 1 (Wire1: SDA=2, SCL=1).
void lateInitVariant()
{
    // I2C: ES8311 lives on internal bus (Wire1)
    PinsAudioBoardES8311.addI2C(PinFunction::CODEC, Wire1);
    // I2S: MCLK, BCK, WS, data_out (speaker), data_in (mic)
    PinsAudioBoardES8311.addI2S(PinFunction::CODEC, DAC_I2S_MCLK, DAC_I2S_BCK, DAC_I2S_WS, DAC_I2S_DOUT, DAC_I2S_DIN);

    CodecConfig cfg;
    cfg.input_device = ADC_INPUT_LINE1;
    cfg.output_device = DAC_OUTPUT_ALL;
    cfg.i2s.bits = BIT_LENGTH_16BITS;
    cfg.i2s.rate = RATE_8K; // Codec2 requires 8 kHz
    board.begin(cfg);
}

#endif
