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

    enum Mode { stereo = 0, mid = 1, side = 2 };

    /** Processes in place. In Mid/Side mode the EQ is applied to only the
        mid (or side) component; in Stereo mode it filters both channels. */
    void process (juce::AudioBuffer<float>& buffer, int mode)
    {
        if (buffer.getNumChannels() < 2 || mode == stereo)
        {
            juce::dsp::AudioBlock<float> block (buffer);
            chain.process (juce::dsp::ProcessContextReplacing<float> (block));
            return;
        }

        auto* L = buffer.getWritePointer (0);
        auto* R = buffer.getWritePointer (1);
        const int n = buffer.getNumSamples();

        for (int i = 0; i < n; ++i)               // encode L/R -> M/S
        {
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]);
            L[i] = m; R[i] = s;
        }

        juce::dsp::AudioBlock<float> block (buffer);
        auto target = block.getSingleChannelBlock (mode == mid ? 0 : 1);
        chain.process (juce::dsp::ProcessContextReplacing<float> (target));

        for (int i = 0; i < n; ++i)               // decode M/S -> L/R
        {
            const float m = L[i];
            const float s = R[i];
            L[i] = m + s; R[i] = m - s;
        }
    }

private:
    enum BandIndex { lowShelf, lowBell, highBell, highShelf };

    using Band = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                juce::dsp::IIR::Coefficients<float>>;

    juce::dsp::ProcessorChain<Band, Band, Band, Band> chain;
    double sampleRate = 44100.0;
};
} // namespace mf
