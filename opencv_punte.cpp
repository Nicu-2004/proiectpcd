// NOLINTBEGIN(misc-include-cleaner, misc-header-include-cycle)
#include <opencv2/core.hpp>      
#include <opencv2/imgproc.hpp>   
#include <opencv2/imgcodecs.hpp> 
#include <vector>
#include <array>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdint> 
#include <cstddef>

using namespace std;
using namespace cv;

struct IntervalIntrebari {
    size_t start;
    size_t final;
};

enum ConfigOMR : std::uint8_t {
    max_intrebari = 40,
    intrebari_coloana = 20,
    threshold_pixeli = 255,
    dim_cerc_min = 40,
    dim_cerc_max = 80,
    gaussian_k = 5,
    adaptive_block = 15,
    adaptive_c = 5
};

const float aspect_min = 0.7F;
const float aspect_max = 1.3F;
const float scala_lungime = 1200.0F;

std::array<int, max_intrebari> raspunsuri_student;
std::array<int, max_intrebari> barem_corect;

int numar_coloana_curenta = 1;

void incarca_barem_din_fisier(const char* nume_fisier) {
    ifstream fisier(nume_fisier);

    if (!fisier.is_open()) {
        // REPARAT: Am scos endl (Eroarea 1 din cele 7)
        cout << "[Eroare] Nu am gasit fisierul " << nume_fisier << "\n";
        for (int &raspuns : barem_corect) {
            raspuns = 0;
        }
        return;
    }

    string linie;
    size_t index_intrebare = 0;

    while (getline(fisier, linie) && index_intrebare < max_intrebari) {
        if (linie.find('A') != string::npos) { barem_corect.at(index_intrebare) = 0; }
        else if (linie.find('B') != string::npos) { barem_corect.at(index_intrebare) = 1; }
        else if (linie.find('C') != string::npos) { barem_corect.at(index_intrebare) = 2; }
        else if (linie.find('D') != string::npos) { barem_corect.at(index_intrebare) = 3; }
        else { barem_corect.at(index_intrebare) = -1; }
        index_intrebare++;
    }
    fisier.close();
}

auto sorteaza_sus_jos(const Rect &rect_a, const Rect &rect_b) -> bool { 
    return rect_a.y < rect_b.y; 
}

auto sorteaza_stanga_dreapta(const Rect &rect_a, const Rect &rect_b) -> bool { 
    return rect_a.x < rect_b.x; 
}

void proceseaza_coloana(const Mat &imagine_thresh, const IntervalIntrebari interval) {
    vector<vector<Point>> contururi;
    findContours(imagine_thresh, contururi, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    vector<Rect> bule;
    for (const auto &contur : contururi) {
        const Rect cutie = boundingRect(contur);
        const auto aspect_ratio = static_cast<float>(cutie.width) / static_cast<float>(cutie.height);

        if (aspect_ratio >= aspect_min && aspect_ratio <= aspect_max && 
            cutie.width > dim_cerc_min && cutie.width < dim_cerc_max) {
            
            if (cutie.x > dim_cerc_min) {
                bule.push_back(cutie);
            }
        }
    }

    Mat poza_debug;
    cvtColor(imagine_thresh, poza_debug, COLOR_GRAY2BGR);
    for (const auto &bula : bule) {
        rectangle(poza_debug, bula, Scalar(0, 0, threshold_pixeli), 2); 
    }

    const string nume_poza = (numar_coloana_curenta == 1) ? "debug_stanga.jpg" : "debug_dreapta.jpg";
    imwrite(nume_poza, poza_debug);
    numar_coloana_curenta++;

    sort(bule.begin(), bule.end(), sorteaza_sus_jos);

    auto q_index = interval.start;
    for (size_t contor = 0; contor + 3 < bule.size() && q_index <= interval.final; contor += 4) {
        // REPARAT: static_cast (Erorile de tip narrowing)
        vector<Rect> rand_curent(bule.begin() + static_cast<long>(contor), 
                                     bule.begin() + static_cast<long>(contor) + 4);
        sort(rand_curent.begin(), rand_curent.end(), sorteaza_stanga_dreapta);

        int pixeli_maximi = 0;
        int raspuns_ales = -1;

        for (int j_pos = 0; j_pos < 4; j_pos++) {
            const Mat interior_bula = imagine_thresh(rand_curent.at(static_cast<size_t>(j_pos)));
            const int pixeli_albi = countNonZero(interior_bula);

            if (pixeli_albi > pixeli_maximi) {
                pixeli_maximi = pixeli_albi;
                raspuns_ales = j_pos; 
            }
        }
        raspunsuri_student.at(q_index) = raspuns_ales;
        q_index++;
    }
}

extern "C" {
    auto incarca_imagine_opencv(const char* cale) -> int {
        incarca_barem_din_fisier("barem.txt");
        numar_coloana_curenta = 1;

        const Mat imagine = imread(cale, IMREAD_COLOR);
        if (imagine.empty()) {
            return 0;
        }

        const auto scala = scala_lungime / static_cast<float>(imagine.rows);
        Mat imagine_redimensionata;
        resize(imagine, imagine_redimensionata, Size(), scala, scala);

        Mat gri; 
        Mat blur; 
        Mat thresh;
        cvtColor(imagine_redimensionata, gri, COLOR_BGR2GRAY);
        GaussianBlur(gri, blur, Size(gaussian_k, gaussian_k), 0);
        adaptiveThreshold(blur, thresh, threshold_pixeli, ADAPTIVE_THRESH_GAUSSIAN_C, 
                          THRESH_BINARY_INV, adaptive_block, adaptive_c);

        const int jumatate_x = thresh.cols / 2;
        const Mat stanga = thresh(Rect(0, 0, jumatate_x, thresh.rows));
        const Mat dreapta = thresh(Rect(jumatate_x, 0, thresh.cols - jumatate_x, thresh.rows));

        proceseaza_coloana(stanga, {0U, static_cast<size_t>(intrebari_coloana - 1)}); 
        proceseaza_coloana(dreapta, {static_cast<size_t>(intrebari_coloana), static_cast<size_t>(max_intrebari - 1)}); 

        return 1; 
    }

    auto proceseaza_intrebare_opencv(int nr_intrebare) -> int {
        const auto index = static_cast<size_t>(nr_intrebare - 1); 
        if (raspunsuri_student.at(index) == -1) {
            return 0;
        } 
        if (raspunsuri_student.at(index) == barem_corect.at(index)) {
            return 1;
        } 
        return 0; 
    }
}
// NOLINTEND(misc-include-cleaner, misc-header-include-cycle)
