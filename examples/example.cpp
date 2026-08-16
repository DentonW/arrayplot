// Demo producer. Start arrayplot_viewer.exe first, then run this -- it
// streams an animated sine wave, and (if Eigen was found) a random matrix,
// so you can see both the line-plot and heatmap paths update live.

#include <arrayplot/arrayplot.h>

#ifdef ARRAYPLOT_HAVE_EIGEN
#include <Eigen/Dense>
#include <arrayplot/arrayplot_eigen.h>
#endif

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

int main() {
    std::vector<double> wave(200);
    for (int frame = 0; frame < 300; ++frame) {
        for (size_t i = 0; i < wave.size(); ++i) {
            wave[i] = std::sin(static_cast<double>(i) * 0.05 + frame * 0.1);
        }
        aplot::plot1d("sine", wave.data(), wave.size());

#ifdef ARRAYPLOT_HAVE_EIGEN
        Eigen::MatrixXd m = Eigen::MatrixXd::Random(20, 30) * (1.0 + (frame % 10));
        aplot::plot("random_matrix", m);
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    return 0;
}
