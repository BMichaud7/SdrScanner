#ifdef _WIN32
#  define _USE_MATH_DEFINES  // enable M_PI on MSVC
#endif
#include "Spectrum.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

Spectrum::Spectrum() {
    in_   = fftwf_alloc_complex(FRAME);
    out_  = fftwf_alloc_complex(FRAME);
    plan_ = fftwf_plan_dft_1d(FRAME, in_, out_, FFTW_FORWARD, FFTW_ESTIMATE);
}
Spectrum::~Spectrum() {
    if (plan_) fftwf_destroy_plan(plan_);
    fftwf_free(in_);
    fftwf_free(out_);
}

std::vector<Signal> Spectrum::analyse(const std::vector<float>& iq,
                                      au::QuantityD<au::Hertz> cf,
                                      au::QuantityD<au::Hertz> sr,
                                      double threshold_db)
{
    int n_samp = (int)iq.size() / 2;
    int step   = FRAME / 2;
    int frames = (n_samp - FRAME) / step;
    if (frames < 4) return {};

    // Blackman window
    std::vector<float> win(FRAME);
    for (int i = 0; i < FRAME; ++i)
        win[i] = 0.42f - 0.5f * std::cos(2*M_PI*i/(FRAME-1))
                        + 0.08f * std::cos(4*M_PI*i/(FRAME-1));

    // Welch accumulate
    std::vector<double> acc(FRAME, 0.0);
    for (int f = 0; f < frames; ++f) {
        int off = f * step * 2;
        for (int i = 0; i < FRAME; ++i) {
            in_[i][0] = iq[off + 2*i]   * win[i];
            in_[i][1] = iq[off + 2*i+1] * win[i];
        }
        fftwf_execute(plan_);
        for (int i = 0; i < FRAME; ++i)
            acc[i] += (double)out_[i][0]*out_[i][0] + (double)out_[i][1]*out_[i][1];
    }
    for (auto& v : acc) v /= frames;

    // FFT-shift → dB
    std::vector<double> psd(FRAME);
    for (int i = 0; i < FRAME; ++i)
        psd[i] = 10.0 * std::log10(acc[(i + FRAME/2) % FRAME] + 1e-30);

    double sr_hz   = sr.in(au::hertz);
    double cf_hz   = cf.in(au::hertz);
    double bin_hz  = sr_hz / FRAME;
    auto   freq_of = [&](int i){ return cf_hz + (i - FRAME/2) * bin_hz; };

    // Noise floor (median) + threshold
    std::vector<double> tmp(psd);
    std::sort(tmp.begin(), tmp.end());
    double noise = tmp[FRAME/2];
    double thr   = noise + threshold_db;

    // Contiguous regions above threshold
    struct Reg { int lo, hi; };
    std::vector<Reg> regs;
    bool in_r = false; int r_lo = 0;
    for (int i = 0; i < FRAME; ++i) {
        if      (psd[i] > thr  && !in_r) { in_r = true;  r_lo = i; }
        else if (psd[i] <= thr &&  in_r) { in_r = false; regs.push_back({r_lo, i-1}); }
    }
    if (in_r) regs.push_back({r_lo, FRAME-1});

    // Regions → signals
    std::vector<Signal> raw;
    for (auto& r : regs) {
        auto  pk  = (int)(std::max_element(psd.begin()+r.lo, psd.begin()+r.hi+1) - psd.begin());
        double f  = freq_of(pk);
        double bw = freq_of(r.hi) - freq_of(r.lo);
        if (bw < 1e3 || bw > 19e6) continue;
        raw.push_back({au::hertz(f), au::hertz(bw), psd[pk]-noise, ""});
    }

    // Merge peaks within 50 kHz (FM pilot / RDS tones collapse into one)
    std::vector<Signal> merged;
    for (auto& s : raw) {
        if (!merged.empty() &&
            std::abs((s.freq - merged.back().freq).in(au::hertz)) < 50e3) {
            auto& m = merged.back();
            double lo = std::min(m.freq.in(au::hertz) - m.bw.in(au::hertz) / 2.0,
                                 s.freq.in(au::hertz) - s.bw.in(au::hertz) / 2.0);
            double hi = std::max(m.freq.in(au::hertz) + m.bw.in(au::hertz) / 2.0,
                                 s.freq.in(au::hertz) + s.bw.in(au::hertz) / 2.0);
            if (s.power_dbc > m.power_dbc) m.freq = s.freq;
            m.bw        = au::hertz(hi - lo);
            m.power_dbc = std::max(m.power_dbc, s.power_dbc);
        } else {
            merged.push_back(s);
        }
    }

    std::vector<Signal> out;
    for (auto& s : merged) {
        if (s.bw.in(au::hertz) < 2e3) continue;
        s.type = classify(s.freq, s.bw);
        out.push_back(s);
    }
    return out;
}

std::string Spectrum::classify(au::QuantityD<au::Hertz> freq,
                                au::QuantityD<au::Hertz> bw)
{
    double f  = freq.in(au::hertz);
    double bw_hz = bw.in(au::hertz);
    if (f >= 87.5e6 && f <= 108e6  && bw_hz >  80e3) return "WFM — Broadcast FM";
    if (f >= 87.5e6 && f <= 108e6)                    return "FM  — Low-power / distant";
    if (f >= 108e6  && f <  118e6  && bw_hz <  30e3) return "AM  — VOR / ILS nav";
    if (f >= 108e6  && f <  118e6)                    return "AM  — Aviation nav";
    if (f >= 118e6  && f <  136e6)                    return "AM  — Aircraft voice";
    if (f >= 136e6  && f <  139e6  && bw_hz >  30e3) return "APT / LRPT — Met satellite";
    if (f >= 136e6  && f <  139e6)                    return "FSK — LEO telemetry";
    if (f >= 144e6  && f <  148e6  && bw_hz <  20e3) return "NFM — 2m amateur";
    if (f >= 144e6  && f <  148e6)                    return "WFM / SSB — 2m amateur";
    if (f >= 148e6  && f <  162e6)                    return "NFM — Public safety / APRS";
    if (f >= 162.3e6 && f <= 162.6e6)                 return "NFM — NOAA weather radio";
    if (f >= 162e6  && f <  174e6)                    return "NFM — VHF public safety";
    if (f >= 174e6  && f <  200e6  && bw_hz >   5e6) return "DVB-T — Digital TV";
    if (f >= 174e6  && f <  200e6)                    return "NFM / Digital — VHF hi";
    if (bw_hz > 100e3) return "WFM — Wideband";
    if (bw_hz >  20e3) return "NFM — Narrowband FM";
    return "AM / SSB — Narrowband";
}
