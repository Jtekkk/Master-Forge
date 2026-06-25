#pragma once

#include <juce_dsp/juce_dsp.h>

namespace mf
{
/**
    Four-band mastering EQ: low shelf, two parametric bells and a high shelf.

    Each band is a second-order IIR filter. ProcessorDuplicator gives every
    channel its own filter state while sharing one set of coefficients.
*/
class ParametricEQ
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        chain.prepare (spec);
    }

    void reset() { chain.reset(); }

    /** Recompute coefficients. Cheap enough to call once per block. */
    void setParameters (float lowFreq,  float lowGainDb,
                        float lmFreq,   float lmGainDb,  float lmQ,
                        float hmFreq,   float hmGainDb,  float hmQ,
                        float highFreq, float highGainDb)
    {
        using Coefs = juce::dsp::IIR::Coefficients<float>;
        const auto sr = sampleRate;

        const auto g = [] (float dB) { return juce::Decibels::decibelsToGain (dB); };

        // Shelves use a fixed, gentle Q that sounds natural for mastering moves.
        *chain.get<lowShelf>().state  = *Coefs::makeLowShelf  (sr, lowFreq,  0.707f, g (lowGainDb));
        *chain.get<lowBell>().state   = *Coefs::makePeakFilter (sr, lmFreq,  lmQ,    g (lmGainDb));
        *chain.get<highBell>().state  = *Coefs::makePeakFilter (sr, hmFreq,  hmQ,    g (hmGainDb));
        *chain.get<highShelf>().state = *Coefs::makeHighShelf (sr, highFreq, 0.707f, g (highGainDb));
    }

    void process (const juce::dsp::ProcessContextReplacing<float>& context)
    {
        chain.process (context);
    }

private:
    enum BandIndex { lowShelf, lowBell, highBell, highShelf };

    using Band = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                juce::dsp::IIR::Coefficients<float>>;

    juce::dsp::ProcessorChain<Band, Band, Band, Band> chain;
    double sampleRate = 44100.0;
};
} // namespace mf
