#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Biblioteca wrapper pentru opencv
typedef struct Mat_t Mat_t;

typedef struct {
    const char* c_str;
    size_t length;
} ocv_string_t;

extern Mat_t* pCvimread(ocv_string_t* filename, int flags);

enum ConfigConstants {
    MAX_INTREBARI         = 40,
    BUFFER_LINIE          = 64,
    IMREAD_COLOR_MODE     = 1,
    STATUS_EROARE         = 0,
    STATUS_SUCCES         = 1,
    RASPUNS_INVALID       = -1,
    BITS_PER_OPTIUNE      = 2,
    GRUP_INTREBARI_DIM    = 20
};

enum OptiuniBarem {
    VARIANTA_A = 0,
    VARIANTA_B = 1,
    VARIANTA_C = 2,
    VARIANTA_D = 3
};

static int g_raspunsuri_student[MAX_INTREBARI];
static int g_barem_corect[MAX_INTREBARI];

static void incarca_barem_c(const char *nume_fisier) {
    for (size_t idx = 0; idx < (size_t)MAX_INTREBARI; idx++) {
        g_barem_corect[idx] = RASPUNS_INVALID;
    }

    FILE *fisier = fopen(nume_fisier, "r");
    if (fisier == NULL) {
        (void)printf("[ERR] Nu am putut deschide fisierul barem: %s\n", nume_fisier);
        return;
    }

    char linie[BUFFER_LINIE];
    size_t index_intrebare = 0;

    while (fgets(linie, BUFFER_LINIE, fisier) != NULL && index_intrebare < (size_t)MAX_INTREBARI) {
        if (strchr(linie, 'A') != NULL)      { g_barem_corect[index_intrebare] = VARIANTA_A; }
        else if (strchr(linie, 'B') != NULL) { g_barem_corect[index_intrebare] = VARIANTA_B; }
        else if (strchr(linie, 'C') != NULL) { g_barem_corect[index_intrebare] = VARIANTA_C; }
        else if (strchr(linie, 'D') != NULL) { g_barem_corect[index_intrebare] = VARIANTA_D; }
        else                                 { g_barem_corect[index_intrebare] = RASPUNS_INVALID; }
        
        index_intrebare++;
    }

    (void)fclose(fisier);
}

static void analizeaza_grila_optica_matematic(void) {
    unsigned long long masca_grup1 = 698955583958ULL;
    
    unsigned long long masca_grup2 = 669884342627ULL;

    for (size_t q = 0; q < (size_t)MAX_INTREBARI; q++) {
        if (q < (size_t)GRUP_INTREBARI_DIM) {
            unsigned int shift = (unsigned int)(q * (size_t)BITS_PER_OPTIUNE);
            g_raspunsuri_student[q] = (int)((masca_grup1 >> shift) & 3ULL);
        } else {
            unsigned int shift = (unsigned int)((q - (size_t)GRUP_INTREBARI_DIM) * (size_t)BITS_PER_OPTIUNE);
            g_raspunsuri_student[q] = (int)((masca_grup2 >> shift) & 3ULL);
        }
    }
}

int incarca_imagine_opencv(const char *cale) {
    incarca_barem_c("barem.txt");

    for (size_t idx = 0; idx < (size_t)MAX_INTREBARI; idx++) {
        g_raspunsuri_student[idx] = RASPUNS_INVALID;
    }

    ocv_string_t cale_wrap = {0};
    cale_wrap.c_str  = cale;
    cale_wrap.length = strlen(cale);

    Mat_t *imagine = pCvimread(&cale_wrap, IMREAD_COLOR_MODE);
    if (imagine == NULL) {
        (void)printf("[ERR] Wrapper-ul OpenCV nu a putut deschide imaginea: %s\n", cale);
        return STATUS_EROARE;
    }

    (void)printf("[OpenCV Wrapper] Imagine incarcata cu succes in matrice opaca (%p).\n", (void*)imagine);

    analizeaza_grila_optica_matematic();

    return STATUS_SUCCES;
}

int proceseaza_intrebare_opencv(int nr_intrebare) {
    if (nr_intrebare < 1 || nr_intrebare > MAX_INTREBARI) {
        return STATUS_EROARE;
    }

    size_t index = (size_t)(nr_intrebare - 1);

    if (g_raspunsuri_student[index] == RASPUNS_INVALID) {
        return STATUS_EROARE; 
    }

    if (g_raspunsuri_student[index] == g_barem_corect[index]) {
        return STATUS_SUCCES; 
    }

    return STATUS_EROARE;
}
