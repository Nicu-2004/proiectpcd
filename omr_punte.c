#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ========================================================
// 1. DECLARAȚII MANUALE PENTRU OPENCV WRAPPER
// ========================================================
struct string_t {
    char* v;
    int nrchar;
};

struct Mat_t;

extern struct string_t* pCvStringCreate(const int nrchar);
extern void pCvStringDelete(struct string_t* wrapper);
extern struct Mat_t* pCvimread(struct string_t* filename, int flags);

extern unsigned char pCvMatGetByte(struct Mat_t* wrapper, int rowind, int colind, int channel);
extern unsigned char pCvMatSetByte(struct Mat_t* wrapper, int rowind, int colind, unsigned char value, int channel);
extern int pCvMatGetWidth(struct Mat_t* wrapper);
extern int pCvMatGetHeight(struct Mat_t* wrapper);
extern void pCvMatDelete(struct Mat_t* wrapper);


// ========================================================
// 2. FUNCȚIE DE SALVARE ÎN PNG PENTRU DEBUG
// ========================================================
void salveaza_imagine_debug(struct Mat_t* imagine, const char* cale_fisier_png) {
    int w = pCvMatGetWidth(imagine);
    int h = pCvMatGetHeight(imagine);
    
    // Salvăm un PGM temporar
    FILE* f = fopen("/tmp/temp_debug.pgm", "wb");
    if (f) {
        fprintf(f, "P5\n%d %d\n255\n", w, h);
        unsigned char* rand_pixeli = malloc(w);
        for(int y = 0; y < h; y++) {
            for(int x = 0; x < w; x++) {
                rand_pixeli[x] = pCvMatGetByte(imagine, y, x, 0);
            }
            fwrite(rand_pixeli, 1, w, f);
        }
        free(rand_pixeli);
        fclose(f);
        
        // Convertim nativ in PNG prin utilitarul sistemului
        char comanda[512];
        sprintf(comanda, "convert /tmp/temp_debug.pgm %s", cale_fisier_png);
        int res = system(comanda);
        
        if(res == 0) {
            printf("[DEBUG-OPENCV] Imaginea calibrata salvata ca PNG: %s\n", cale_fisier_png);
        } else {
            printf("[DEBUG-OPENCV] EROARE: Verifica existenta ImageMagick (sudo apt install imagemagick)\n");
        }
    }
}

// ========================================================
// 3. FUNCȚIILE AȘTEPTATE DE SERVERGRADING.C
// ========================================================
struct Mat_t* incarca_imagine_opencv(const char* cale_fisier) {
    int lungime = strlen(cale_fisier);
    struct string_t* cale_wrapper = pCvStringCreate(lungime);
    strcpy(cale_wrapper->v, cale_fisier); 
    
    struct Mat_t* img = pCvimread(cale_wrapper, 0); 
    pCvStringDelete(cale_wrapper);
    
    return img;
}

int proceseaza_intrebare_opencv(struct Mat_t* imagine, int index_intrebare, char raspuns_corect) {
    if (imagine == NULL) return 0;

    int w = pCvMatGetWidth(imagine);
    int h = pCvMatGetHeight(imagine);

    int offset_optiune = raspuns_corect - 'A'; 
    int coloana = index_intrebare / 20;        
    int rand_intrebare = index_intrebare % 20;

    // Coordonatele Bulinelor pentru a putea face verificarea
    // Inceputul bulinelor pe imagine
    int START_X = 55;         // Axa X
    int START_Y = 10;         // Axa Y
    
    // Distantele dintre coloane,randuri si linii
    int SPATIU_COLOANE = 291;
    int SPATIU_RANDURI = 31;  
    int SPATIU_LITERE = 30;   
    
    int dimensiune_bula = 30;
    
    int x_start = START_X + (coloana * SPATIU_COLOANE) + (offset_optiune * SPATIU_LITERE); 
    int y_start = START_Y + (rand_intrebare * SPATIU_RANDURI);
    
    int pixeli_negri = 0;

    if (!(x_start < 0 || y_start < 0 || x_start + dimensiune_bula >= w || y_start + dimensiune_bula >= h)) {
        for(int y = y_start; y < y_start + dimensiune_bula; y++) {
            for(int x = x_start; x < x_start + dimensiune_bula; x++) {
                
                unsigned char pixel = pCvMatGetByte(imagine, y, x, 0);
                if(pixel < 128) { 
                    pixeli_negri++;
                }
                
                // Desenam chenarul negru pentru vizualizare
                if (y == y_start || y == y_start + dimensiune_bula - 1 ||
                    x == x_start || x == x_start + dimensiune_bula - 1) {
                    pCvMatSetByte(imagine, y, x, 0, 0); 
                }
            }
        }
    }

    if (index_intrebare == 39) {
        salveaza_imagine_debug(imagine, "/tmp/debug_calibrare.png");
    }

    // Numarul de pixeli negri minimi care trebuie sa aiba bula pentru a fi considerat raspuns corect
    if (pixeli_negri > 200) {
        return 1;
    }
    return 0;
}
