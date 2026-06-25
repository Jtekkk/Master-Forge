#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Compressor.h"

namespace mf
{
/**
    3-band multiband compressor.

    The signal is split into low/mid/high bands with 4th-order Linkwitz-Riley
    crossovers and each band is compressed independently. An allpass on the low
    band matches the phase shift the second crossover adds to the mid/high path,
    so summing the bands reconstructs flat. Attack/release/knee are shared;
    threshold/ratio/makeup are per band.
*/
class MultibandCompressor
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        for (auto* f : { &lpLow, &apLow, &hpA, &lpMid, &hpHigh })
            f->prepare (spec);

        lpLow.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
        apLow.setType  (juce::dsp::LinkwitzRileyFilterType::allpass);
        hpA.setType    (juce::dsp::LinkwitzRileyFilterType::highpass);
        lpMid.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
        hpHigh.setType (juce::dsp::LinkwitzRileyFilterType::highpass);

        compLow.prepare (spec);
        compMid.prepare (spec);
        compHigh.prepare (spec);

        const int ch = (int) spec.numChannels;
        const int n  = (int) spec.maximumBlockSize;
        lowBuf.setSize (ch, n);
        midBuf.setSize (ch, n);
        highBuf.setSize (ch, n);

        reset();
    }

    void reset()
    {
        lpLow.reset(); apLow.reset(); hpA.reset(); lpMid.reset(); hpHigh.reset();
        compLow.reset(); compMid.reset(); compHigh.reset();
    }

    void setParameters (float xLow, float xHigh,
                        float attackMs, float releaseMs, float kneeDb,
                        float loThr, float loRatio, float loMakeup,
                        float mdThr, float mdRatio, float mdMakeup,
                        float hiThr, float hiRatio, float hiMakeup)
    {
        const float nyq = (float) sampleRate * 0.49f;
        xLow  = juce::jlimit (20.0f, nyq - 20.0f, xLow);
        xHigh = juce::jlimit (xLow + 20.0f, nyq, xHigh);

        lpLow.setCutoffFrequency (xLow);
        hpA.setCutoffFrequency   (xLow);
        apLow.setCutoffFrequency  (xHigh);
        lpMid.setCutoffFrequency  (xHigh);
        hpHigh.setCutoffFrequency (xHigh);

        compLow.setParameters  (loThr, loRatio, attackMs, releaseMs, kneeDb, loMakeup);
        compMid.setParameters  (mdThr, mdRatio, attackMs, releaseMs, kneeDb, mdMakeup);
        compHigh.setParameters (hiThr, hiRatio, attackMs, releaseMs, kneeDb, hiMakeup);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int ch = buffer.getNumChannels();
        const int n  = buffer.getNumSamples();

        for (int c = 0; c < ch; ++c)
        {
            lowBuf.copyFrom (c, 0, buffer, c, 0, n);
            highBuf.copyFrom (c, 0, buffer, c, 0, n);
        }

        // Low band = LP(xLow), phase-aligned with the second crossover.
        applyFilter (lpLow, lowBuf);
        applyFilter (apLow, lowBuf);

        // Everything above xLow, then split at xHigh into mid and high.
        applyFilter (hpA, highBuf);
        for (int c = 0; c < ch; ++c)
            midBuf.copyFrom (c, 0, highBuf, c, 0, n);
        applyFilter (lpMid, midBuf);    // mid
        applyFilter (hpHigh, highBuf);  // high

        grLow  = compLow.process (lowBuf);
        grMid  = compMid.process (midBuf);
        grHigh = compHigh.process (highBuf);

        for (int c = 0; c < ch; ++c)
        {
            buffer.copyFrom (c, 0, lowBuf, c, 0, n);
            buffer.addFrom  (c, 0, midBuf, c, 0, n);
            buffer.addFrom  (c, 0, highBuf, c, 0, n);
        }
    }

    float getReductionLow()  const noexcept { return grLow; }
    float getReductionMid()  const noexcept { return grMid; }
    float getReductionHigh() const noexcept { return grHigh; }

private:
    void applyFilter (juce::dsp::LinkwitzRileyFilter<float>& f, juce::AudioBuffer<float>& b)
    {
        const int ch = b.getNumChannels();
        const int n  = b.getNumSamples();
        for (int c = 0; c < ch; ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < n; ++i)
                d[i] = f.processSample (c, d[i]);
        }
    }

    double sampleRate = 44100.0;

    juce::dsp::LinkwitzRileyFilter<float> lpLow, apLow, hpA, lpMid, hpHigh;
    Compressor compLow, compMid, compHigh;
    juce::AudioBuffer<float> lowBuf, midBuf, highBuf;

    float grLow = 0.0f, grMid = 0.0f, grHigh = 0.0f;
};
} // namespace mf
