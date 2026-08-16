// Demo producer showing only the Eigen matrix path. Start arrayplot_viewer.exe
// first, then run this -- it streams a fresh random matrix a few times a
// second, so you can watch the heatmap update live.

#include <arrayplot/arrayplot.h>

#include <Eigen/Dense>
#include <arrayplot/arrayplot_eigen.h>

#include <chrono>
#include <thread>

int main() {
    for (int frame = 0; frame < 300; ++frame) {
        Eigen::MatrixXd m = Eigen::MatrixXd::Random(25, 40);
        aplot::plot("random_matrix", m);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}
