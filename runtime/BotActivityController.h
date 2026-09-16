#pragma once
#include <algorithm>
#include <cmath>

namespace TortoiseBots {
// Module-owned version of the preserved random-population PID policy. The
// world maintenance owner publishes one percentage; AI priority brackets decide
// which activities may be reduced. This never changes core map/session cadence.
class BotActivityController
{
public:
    bool Configure(double p, double i, double d)
    {
        if (!std::isfinite(p) || !std::isfinite(i) || !std::isfinite(d)) return false;
        m_p = p; m_i = i; m_d = d;
        m_integral = m_previous = 0;
        return true;
    }
    double Update(double targetMs, double measuredMs, double elapsedSeconds)
    {
        if (!std::isfinite(targetMs) || !std::isfinite(measuredMs) ||
            !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0) return m_percentage;
        double const error = targetMs - measuredMs;
        double const integral = m_integral + error * elapsedSeconds;
        double const output = m_p * error + m_i * integral +
            m_d * (error - m_previous) / elapsedSeconds;
        // Invalid tuning arithmetic must not poison later updates. Saturation
        // retains the previous integral, as in the original anti-windup policy.
        if (!std::isfinite(output)) return m_percentage;
        if (output >= -50 && output <= 50) m_integral = integral;
        m_previous = error;
        m_percentage = std::clamp(output + 50, 0.0, 100.0);
        return m_percentage;
    }
    double Percentage() const { return m_percentage; }
private:
    double m_p = 0.05, m_i = 0.001, m_d = 0.05;
    double m_integral = 0, m_previous = 0, m_percentage = 100;
};
}
