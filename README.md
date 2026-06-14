#  Grading App 

[![Language](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B-00599C?style=flat&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Library](https://img.shields.io/badge/Library-OpenCV-5C3EE8?style=flat&logo=opencv)](https://opencv.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux-FCC624?style=flat&logo=linux&logoColor=black)](https://www.linux.org/)

**Grading App** este o aplicație distribuită de tip **Client-Server**, dezvoltată în C/C++ și inspirată de soluții computerizate de procesare a testelor precum *ZipGrade*. Scopul principal al sistemului este automatizarea procesului de corectare a testelor grilă prin tehnologii de recunoaștere optică a marcajelor (**OMR - Optical Mark Recognition**) și utilizarea programării concurente la nivelul sistemului de operare.

---

## 1. Introducere

Aplicația primește fișiere imagine ce reprezintă foile de răspuns completate de studenți, procesează bulele marcate utilizând biblioteca **OpenCV**, le compară cu un barem prestabilit și returnează nota finală. 

Un element arhitectural central este **evaluarea paralelizată**: pentru a maximiza performanța și a reduce timpul de răspuns, evaluarea fiecărei întrebări din grilă are loc într-un proces separat al sistemului de operare, rulat în paralel.

---

## 2. Arhitectura Sistemului

Sistemul este construit pe o arhitectură hibridă de tip **TCP Cerere-Răspuns**, cuplată cu o punte în C++ pentru procesarea de imagini și un model concurent de tip **multi-process** pentru evaluarea propriu-zisă a grilei.

### 2.1. Componentele Arhitecturale

* **TCP Client (`clientgrading.c`):** Interfața cu utilizatorul. Preia calea imaginii testului de la utilizator, transmite cererea de evaluare către server și afișează rezultatul final primit.
* **TCP Server (`servergrading.c`):** Nodul central de procesare. Așteaptă conexiuni de la clienți, parsează cererile sosite în rețea și orchestrează corectarea.
* **OpenCV Bridge (`opencv_punte.cpp`):** Modulul dedicat procesării vizuale. Încarcă baremul oficial, aplică filtre pe imagine, identifică contururile cercurilor și determină care dintre ele au fost marcate de student.
* **Sub-sistemul de Concurrency:** Serverul folosește apelul de sistem `fork()` pentru a crea **40 de procese fiu** (câte unul pentru fiecare întrebare din test). Comunicarea rezultatelor de la procesele fiu înapoi către procesul părinte se realizează prin **Pipes anonime**.

### 2.2. Fluxul de Date

1. Clientul TCP trimite mesajul `grade:cale_imagine` către Serverul TCP.
2. Serverul TCP apelează funcția de încărcare din Modulul OpenCV.
3. Modulul OpenCV returnează matricea de răspunsuri în memorie.
4. Serverul TCP creează 40 de procese fiu folosind apeluri succesive `fork()`.
5. Fiecare Proces Fiu interoghează Modulul OpenCV pentru întrebarea sa specifică și scrie `1` (corect) sau `0` (greșit) în Pipe-ul alocat.
6. Serverul TCP (Părintele) citește din toate cele 40 de Pipes, adună rezultatele și calculeaza nota finală.
7. Serverul TCP trimite nota finală înapoi la Clientul TCP.

---

## 3. Structura Mesajelor

Comunicarea se realizează prin pachete de text brut transmise peste protocolul TCP. Dimensiunea maximă a bufferului de rețea este de **4096 bytes**.

### 3.1 Cereri (Client ➔ Server)

* **Cerere de Evaluare:**
    * **Format:** `grade:cale_absoluta_sau_relativa_imagine`
    * **Descriere:** Instruiește serverul să corecteze imaginea specificată de la calea transmisă.
* **Cerere de Ieșire:**
    * **Format:** `quit`
    * **Descriere:** Închide conexiunea cu serverul în condiții de siguranță.

### 3.2 Răspunsuri (Server ➔ Client)

| Tip Răspuns | Format / Structură |
| :--- | :--- |
| **Răspuns de Succes** | `Nota: X.Y / 10` |
| **Eroare Imagine** | `Eroare: Imaginea nu a putut fi procesata!` |
| **Eroare Sintaxă** | `Comenzi valide: grade:cale_imagine \| quit` |
| **Deconectare** | `La revedere!` |

---

## 4. Specificații API & Date Interne

* **Protocol:** TCP pe Portul `9090`
* **Format date:** Text brut (Null-terminated / `\0`)

### Structura Conceptuală Payload

#### Cerere (Request):
* `command`: `"grade"`
* `delimiter`: `":"`
* `payload`: calea imaginii localizată pe server

#### Răspuns (Response):
* `prefix`: `"Nota: "`
* `grade`: valoare numerică între `0.0` și `10.0`
* `suffix`: `" / 10"`

### Structura Datelor Interne
* `raspunsuri_student`: Array cu 40 de elemente populat de OpenCV care conține opțiunile detectate în urma analizării bulelor (`0=A`, `1=B`, `2=C`, `3=D`, `-1=Gol/Eroare`).
* `barem_corect`: Array cu 40 de elemente populat la pornire prin citirea fișierului `barem.txt`.

### Extensibilitate Viitoare
1.  **Multiple Choice Support:** Adaptarea modulului OpenCV pentru a stoca un vector de biți per rând în loc de o singură valoare, permițând bifarea mai multor răspunsuri per întrebare.
2.  **Reports (Class Summary):** Implementarea unui nou mesaj API, prin care procesul părinte agregă notele salvate pe disc ale tuturor studenților dintr-o clasă și calculează statistici (medie, promovabilitate).

---

## 5. Tehnologii și Biblioteci Utilizate

### Limbaje de Programare
* **C:** Folosit pentru logica de rețea (TCP sockets), managementul proceselor (`fork`, `waitpid`) și comunicarea inter-procese (`pipes`). Fișierele `clientgrading.c` și `servergrading.c` sunt scrise în C pur.
* **C++:** Folosit exclusiv pentru modulul de procesare a imaginilor (`opencv_punte.cpp`), deoarece biblioteca OpenCV oferă o interfață nativă mult mai robusta și optimizată în C++.

### Biblioteci și API-uri
* **OpenCV (Open Source Computer Vision Library):** Baza procesării vizuale. Se folosesc funcții pentru citirea imaginii (`imread`), conversia în grayscale (`cvtColor`), filtrare (`GaussianBlur`), binarizare (`adaptiveThreshold`) și detecția contururilor (`findContours`).
* **POSIX API / Standard C Library:**
    * `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`: Pentru implementarea comunicării în rețea prin socket-uri TCP.
    * `<unistd.h>`, `<sys/wait.h>`: Pentru crearea proceselor paralele (`fork`), sincronizare (`waitpid`) și canale de comunicare (`pipe`).
    * `<stdio.h>`, `<stdlib.h>`, `<string.h>`: Pentru operații standard I/O și manipularea memoriei.
* **Standard Template Library (STL) din C++:** Se utilizează structurile `std::vector`, `std::string`, `std::fstream` și header-ul `<algorithm>` (pentru sortarea bulelor detectate în foaia de răspuns).

---

## 6. Descrierea Codului Sursa

### Clientul (`clientgrading.c`)
Acesta este punctul de interacțiune cu utilizatorul. Inițializează un socket TCP și se conectează la adresa IP și portul serverului. Rulează o buclă infinită în care afișează un meniu simplu. Când utilizatorul introduce calea către o imagine, clientul împachetează acest text și îl trimite serverului, blocându-se apoi (prin `recv`) până când primește nota finală sau un mesaj de eroare.

### Serverul (`servergrading.c`)
Este inima aplicației. Rulează în mod continuu (daemon/loop), așteptând conexiuni de la clienți prin intermediul funcțiilor standard `bind`, `listen` și `accept`. Când primește o cerere de corectare, apelează întâi puntea OpenCV pentru a extrage răspunsurile din imagine.

Partea centrală de programare concurentă are loc aici: serverul creează un array de pipe-uri și apelează funcția `fork()` de 40 de ori (corespunzător celor 40 de întrebări). Fiecare proces fiu primește un ID de întrebare, verifică prin funcția C++ dacă răspunsul studentului se potrivește cu cel din barem și scrie rezultatul binar (`1` sau `0`) în pipe-ul său, după care execută `exit()`. Procesul părinte colectează asincron datele din toate pipe-urile, așteaptă terminarea tuturor fiilor cu `waitpid`, calculează nota finală și trimite rezultatul înapoi clientului sub formă de text formatat.

### Puntea OpenCV (`opencv_punte.cpp`)
Acest fișier face legătura între codul C al serverului și biblioteca C++ OpenCV, expunând funcțiile prin directiva `extern "C"` pentru a putea fi link-ate și apelate direct din `servergrading.c`.

Funcția principală citește întâi fișierul `barem.txt`. Apoi, încarcă imaginea testului în memorie, îi aplică o redimensionare (`resize`) și o serie de filtre: o transformă în format grayscale, aplică un Gaussian blur pentru netezire și un Adaptive Threshold pentru binarizare (alb-negru complet), scoțând în evidență zonele completate cu cerneală. Imaginea este apoi tăiată matematic în două jumătăți (coloana stângă și coloana dreaptă).

Pentru fiecare coloană se caută contururi circulare. Se filtrează doar contururile care au formă apropiată de un cerc și o anumită plajă de dimensiuni. Aceste contururi sunt sortate întâi pe axa Y (pe rânduri) și apoi pe axa X (opțiunile A, B, C, D). Pentru fiecare set de 4 bule, programul izolează interiorul fiecăreia, numără pixelii negri (reprezentând marcajul) și alege bula cu cei mai mulți pixeli completați ca fiind răspunsul ales de student. Acest răspuns este salvat în array-ul global accesat ulterior de procesele fiu din server.

---

## 7. Compilare și Rulare

Deoarece proiectul combină cod scris în C și C++, procesul de compilare pentru server se realizează în mai multe etape (compilare separată a modulelor în fișiere obiect `.o`, urmată de faza de linking).

### Dependențe (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y gcc g++ libopencv-dev pkg-config
Compilare Surse
Bash
# 1. Compilarea serverului în fișier obiect
gcc -c servergrading.c -o servergrading.o

# 2. Compilarea punții OpenCV în fișier obiect (cu flag-urile OpenCV necesare)
g++ -c opencv_punte.cpp -o opencv_punte.o $(pkg-config --cflags opencv4)

# 3. Legarea (Linking) obiectelor într-un singur executabil final pentru Server
g++ servergrading.o opencv_punte.o -o server_grading $(pkg-config --libs opencv4)

# 4. Compilarea executabilului pentru Client
gcc clientgrading.c -o client_grading
Ghid de Execuție
Pasul 1: Pornirea Serverului (Terminal 1)

Bash
./server_grading
Serverul va afișa un mesaj de confirmare a pornirii pe portul 9090 și va intra în starea de așteptare pasivă (listen).

Pasul 2: Pornirea Interfeței Client (Terminal 2)

Bash
./client_grading
După conectarea cu succes, clientul va afișa meniul interactiv. Tastați cifra corespunzătoare acțiunii dorite, apăsați Enter, iar apoi introduceți calea către fișierul imagine (ex: imagini/test_student.png) pentru a declanșa procesul automat de corectare.
