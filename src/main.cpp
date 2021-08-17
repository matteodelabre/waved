#include "waveform_table.hpp"
#include <iostream>
#include <string>

int main(int argc, const char** argv)
{
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0] << " file mode temp from to\n";
        return 1;
    }

    auto table = WaveformTable::from_wbf(argv[1]);
    int mode = std::stoi(argv[2]);
    int temperature = std::stoi(argv[3]);
    int from = std::stoi(argv[4]);
    int to = std::stoi(argv[5]);

    if (from < 0 || from >= 32) {
        std::cerr << "Error: Intensity must be between 0 and 31, got "
            << from << '\n';
        return 1;
    }

    if (to < 0 || to >= 32) {
        std::cerr << "Error: Intensity must be between 0 and 31, got "
            << to << '\n';
        return 1;
    }

    try {
        table.set_mode(mode);
        table.set_temperature(temperature);
    } catch (const std::out_of_range& err) {
        std::cerr << "Error: " << err.what() << '\n';
        return 1;
    }

    const auto& waveform = table.lookup();
    std::cerr << "Phases count: " << waveform.size() << '\n';
    std::cerr << "Waveform: ";

    for (const PhaseMatrix& matrix : waveform) {
        std::cerr << (int) matrix[from][to] << ' ';
    }

    std::cerr << '\n';
    return 0;
}
