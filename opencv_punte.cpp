#include <opencv2/opencv.hpp>
#include <vector>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;
using namespace cv;

// raspunsurile bifate si cele corecte 
int raspunsuri_student[40];
int barem_corect[40]; 

// variabila pentru pozele de debug
int numar_coloana_curenta = 1;

// citirea baremului (raspunsurile corecte)
void incarca_barem_din_fisier(const char* nume_fisier) {
    ifstream fisier(nume_fisier);

    // mesaj daca nu exista un fisier cu numele introdus
    if (!fisier.is_open()) {
        cout << "[Eroare] Nu am gasit fisierul " << nume_fisier << endl;
        for(int i=0; i<40; i++) barem_corect[i] = 0;
        return;
    }
    string linie;
    int index_intrebare = 0;

    // citim baremul linie cu linie
    // TO-DO numar de intrebari dinamic
    while (getline(fisier, linie) && index_intrebare < 40) {
        if (linie.find('A') != string::npos) barem_corect[index_intrebare] = 0;
        else if (linie.find('B') != string::npos) barem_corect[index_intrebare] = 1;
        else if (linie.find('C') != string::npos) barem_corect[index_intrebare] = 2;
        else if (linie.find('D') != string::npos) barem_corect[index_intrebare] = 3;
        else barem_corect[index_intrebare] = -1; 
        index_intrebare++;
    }
    fisier.close(); // inchidem fisierul
}

// sortarea cercurilor pe randuri si de la stanga la dreapta
bool sorteaza_sus_jos(Rect a, Rect b) { return a.y < b.y; }
bool sorteaza_stanga_dreapta(Rect a, Rect b) { return a.x < b.x; }

void proceseaza_coloana(Mat imagine_thresh, int start_q, int end_q) {
    vector<vector<Point>> contururi;
    findContours(imagine_thresh, contururi, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    vector<Rect> bule;
    for (size_t i = 0; i < contururi.size(); i++) {
        Rect cutie = boundingRect(contururi[i]);
        float aspect_ratio = (float)cutie.width / cutie.height;

        if (aspect_ratio >= 0.7 && aspect_ratio <= 1.3 && cutie.width > 40 && cutie.width < 80) {
            
            if (cutie.x > 40) {
                bule.push_back(cutie);
            }
        }
    }

    // DEBUG: ce vede calculatorul
    Mat poza_debug;
    cvtColor(imagine_thresh, poza_debug, COLOR_GRAY2BGR);
    for(size_t i = 0; i < bule.size(); i++) {
        // desenam peste fiecare cerc vazut de calculator
        rectangle(poza_debug, bule[i], Scalar(0, 0, 255), 2); 
    }
    // salvam poza pentru a vedea posibile probleme
    string nume_poza = (numar_coloana_curenta == 1) ? "debug_stanga.jpg" : "debug_dreapta.jpg";
    imwrite(nume_poza, poza_debug);
    numar_coloana_curenta++;

    sort(bule.begin(), bule.end(), sorteaza_sus_jos);

    int q_index = start_q;
    // luam grupele de cercuri
    for (size_t i = 0; i + 3 < bule.size() && q_index <= end_q; i += 4) {
    
        // facem randuri a cate 4 bule
        vector<Rect> rand_curent(bule.begin() + i, bule.begin() + i + 4);
        
        // sortam stanga -> dreapta pentru corectare A -> D
        sort(rand_curent.begin(), rand_curent.end(), sorteaza_stanga_dreapta);

        int pixeli_maximi = 0;
        int raspuns_ales = -1;

        // parcurgem randul
        for (int j = 0; j < 4; j++) {
            Mat interior_bula = imagine_thresh(rand_curent[j]);
            int pixeli_albi = countNonZero(interior_bula);

            // verificam care este incercuit datorita numerelor de pixeli pe rand
            if (pixeli_albi > pixeli_maximi) {
                pixeli_maximi = pixeli_albi;
                raspuns_ales = j; 
            }
        }
        // salvam raspunsul bifat
        raspunsuri_student[q_index] = raspuns_ales;
        q_index++;
    }
}

// legatura cu serverul .C
extern "C" {
    int incarca_imagine_opencv(const char* cale) {
        incarca_barem_din_fisier("barem.txt");
        numar_coloana_curenta = 1;

        Mat imagine = imread(cale, IMREAD_COLOR);
        if (imagine.empty()) return 0;

        float scala = 1200.0f / imagine.rows;
        resize(imagine, imagine, Size(), scala, scala);

        Mat gri, blur, thresh;
        cvtColor(imagine, gri, COLOR_BGR2GRAY);
        GaussianBlur(gri, blur, Size(5, 5), 0);
        adaptiveThreshold(blur, thresh, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY_INV, 15, 5);

        int jumatate_x = thresh.cols / 2;
        Mat stanga = thresh(Rect(0, 0, jumatate_x, thresh.rows));
        Mat dreapta = thresh(Rect(jumatate_x, 0, thresh.cols - jumatate_x, thresh.rows));

        proceseaza_coloana(stanga, 0, 19); 
        proceseaza_coloana(dreapta, 20, 39); 

        return 1; 
    }

    int proceseaza_intrebare_opencv(int nr_intrebare) {
        int index = nr_intrebare - 1; 
        if (raspunsuri_student[index] == -1) return 0; 
        if (raspunsuri_student[index] == barem_corect[index]) return 1; 
        return 0; 
    }
}
