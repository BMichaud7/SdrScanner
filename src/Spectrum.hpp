#pragma once
#include <vector>
#include <string>
#include <fftw3.h>

struct Signal {
    double      freq_mhz  = 0;
    double      bw_khz    = 0;
    double      power_dbc = 0;
    std::string type;
};

class Spectrum {
public:
    static constexpr int FRAME = 8192;

    Spectrum();
    ~Spectrum();

    // Welch-averaged PSD → detected signals.
    // iq: interleaved CF32 samples, cf_hz: centre freq, sr_hz: sample rate
    std::vector<Signal> analyse(const std::vector<float>& iq,
                                double cf_hz, double sr_hz,
                                double threshold_db = 10.0);

private:
    fftwf_plan     plan_ = nullptr;
    fftwf_complex* in_   = nullptr;
    fftwf_complex* out_  = nullptr;

    static std::string classify(double freq_hz, double bw_hz);
};
