#pragma once
#include <vector>
#include <string>
#include <fftw3.h>
#include "au/units/hertz.hh"
#include "au/units/seconds.hh"
#include "au/prefix.hh"

struct Signal {
    au::QuantityD<au::Hertz> freq     = au::hertz(0.0);   // stored in Hz
    au::QuantityD<au::Hertz> bw       = au::hertz(0.0);   // stored in Hz
    double                   power_dbc = 0;
    std::string              type;
};

class Spectrum {
public:
    static constexpr int FRAME = 8192;

    Spectrum();
    ~Spectrum();

    // Welch-averaged PSD → detected signals.
    // iq: interleaved CF32 samples, cf_hz: centre freq, sr_hz: sample rate
    std::vector<Signal> analyse(const std::vector<float>& iq,
                                au::QuantityD<au::Hertz> cf,
                                au::QuantityD<au::Hertz> sr,
                                double threshold_db = 10.0);

private:
    fftwf_plan     plan_ = nullptr;
    fftwf_complex* in_   = nullptr;
    fftwf_complex* out_  = nullptr;

    static std::string classify(au::QuantityD<au::Hertz> freq,
                                au::QuantityD<au::Hertz> bw);
};
