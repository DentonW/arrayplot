// Demo producer showing the Eigen matrix path, real and complex. Start
// arrayplot_viewer.exe first, then run this -- it streams a fresh random
// matrix a few times a second, so you can watch the heatmaps update live.

#include <arrayplot/arrayplot.h>

#include <Eigen/Dense>
#include <arrayplot/arrayplot_eigen.h>

#include <chrono>
#include <complex>
#include <thread>

int main() {
    for (int frame = 0; frame < 300; ++frame) {
        Eigen::MatrixXd m = Eigen::MatrixXd::Random(25, 40);
        aplot::plot("random_matrix", m);

        // Complex: plot() sends both magnitude and phase by default (two
        // viewer windows). Use plot_real/plot_imag directly instead if
        // that's the view you actually want.
        Eigen::MatrixXcd c(25, 40);
        c.real() = Eigen::MatrixXd::Random(25, 40);
        c.imag() = Eigen::MatrixXd::Random(25, 40);
        //aplot::plot("random_complex_matrix", c);
        aplot::plot_magnitude("random_complex_matrix_magnitude", c);
        aplot::plot_phase("random_complex_matrix_phase", c);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
