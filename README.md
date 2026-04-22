Documentatie Arhitecturala: Grading App

1. Introducere
   Grading App este o aplicatie distribuita de tip Client-Server, dezvoltata in C/C++, inspirata de aplicatii precum ZipGrade. Scopul principal al sistemului este automatizarea procesului de corectare a testelor grila cu ajutorul tehnologiei de detectare a imaginilor (Image Processing / OMR - Optical Mark Recognition) si a programarii concurente.

Aplicatia primeste fisiere imagine reprezentand foile de raspuns ale studentilor, proceseaza bulele marcate utilizand OpenCV, le compara cu un barem prestabilit si returneaza nota finala. Un element arhitectural central este evaluarea paralelizata: pentru a maximiza performanta, evaluarea fiecarei intrebari din grila are loc intr-un proces separat al sistemului de operare.

2. Arhitectura Sistemului
   Sistemul este construit pe o arhitectura TCP de tip cerere-raspuns, cuplata cu o punte C++ pentru procesarea imaginilor si un model multi-process pentru evaluare.

2.1. Componentele Arhitecturale
TCP Client (clientgrading.c): Interfata cu utilizatorul. Preia calea imaginii testului de la utilizator, trimite cererea de evaluare catre server si afiseaza rezultatul primit.

TCP Server (servergrading.c): Nodul central de procesare. Asteapta conexiuni de la clienti, parseaza cererile si orchestreaza corectarea.

OpenCV Bridge (opencv_punte.cpp): Modulul de procesare vizuala. Incarca baremul, aplica filtre pe imagine, gaseste contururile cercurilor si determina care au fost marcate.

Sub-sistemul de Concurrency: Serverul foloseste apelul de sistem fork pentru a crea 40 de procese fiu, cate unul pentru fiecare intrebare. Comunicarea rezultatelor de la procesele fiu catre procesul parinte se realizeaza prin Pipes anonime.

2.2. Diagrama de Arhitectura (Fluxul de date)
Clientul TCP trimite mesajul "grade:cale_imagine" catre Serverul TCP.

Serverul TCP apeleaza functia de incarcare din Modulul OpenCV.

Modulul OpenCV returneaza matricea de raspunsuri in memorie.

Serverul TCP creeaza 40 de procese fiu.

Fiecare Proces Fiu interogheaza Modulul OpenCV pentru intrebarea sa si scrie 1 (corect) sau 0 (gresit) in Pipe.

Serverul TCP citeste din Pipes, aduna rezultatele si calculeaza nota.

Serverul TCP trimite nota finala inapoi la Clientul TCP.

3. Structura Mesajelor
   Comunicarea se face prin pachete de text brut peste protocolul TCP. Dimensiunea maxima a bufferului este de 4096 bytes.

3.1 Cereri (Client catre Server)
Cerere de Evaluare:
Format: grade:cale_absoluta_sau_relativa_imagine
Descriere: Instruieste serverul sa corecteze imaginea specificata.

Cerere de Iesire:
Format: quit
Descriere: Inchide conexiunea cu serverul in siguranta.

3.2 Raspunsuri (Server catre Client)
Raspuns de Succes:
Format: Nota: X.Y / 10

Eroare Imagine:
Format: Eroare: Imaginea nu a putut fi procesata!

Eroare Sintaxa:
Format: Comenzi valide: grade:cale_imagine | quit

Deconectare:
Format: La revedere!

4. Specificatii API
   Protocol: TCP pe Port 9090
   Format: Text brut / null-terminated

Actiunea: Corectare Grila
Structura conceptuala a cererii (Request Payload):

command: "grade"

delimiter: ":"

payload: calea imaginii pe server

Structura conceptuala a raspunsului (Response Payload):

prefix: "Nota: "

grade: valoare numerica intre 0.0 si 10.0

suffix: " / 10"

Structura Datelor Interne
raspunsuri_student: Array cu 40 de elemente populat de OpenCV care contine optiunile detectate (0=A, 1=B, 2=C, 3=D, -1=Gol/Eroare).

barem_corect: Array cu 40 de elemente populat din fisierul barem.txt.

Extensibilitate Viitoare
Multiple Choice Support: Adaptarea modulului OpenCV pentru a stoca un vector de biti per rand in loc de o singura valoare, permitand bifarea mai multor raspunsuri.

Reports (Class Summary): Implementarea unui nou mesaj API, prin care procesul parinte agrega notele salvate pe disc ale tuturor studentilor dintr-o clasa si calculeaza statistici.

5. Tehnologii si Biblioteci Utilizate
   Limbaje de Programare
   C: Folosit pentru logica de retea (TCP sockets), managementul proceselor (fork, waitpid) si comunicarea inter-procese (pipes). Fisierele clientgrading.c si servergrading.c sunt scrise in C pur.

C++: Folosit exclusiv pentru modulul de procesare a imaginilor (opencv_punte.cpp), deoarece biblioteca OpenCV ofera o interfata nativa si mult mai robusta in C++.

Biblioteci si API-uri
OpenCV (Open Source Computer Vision Library): Baza procesarii vizuale. Se folosesc functii pentru citirea imaginii (imread), conversia in alb-negru (cvtColor), filtrare (GaussianBlur), binarizare (adaptiveThreshold) si detectia contururilor (findContours).

POSIX API / Standard C Library:

sys/socket.h, netinet/in.h, arpa/inet.h: Pentru implementarea comunicarii in retea (TCP).

unistd.h, sys/wait.h: Pentru crearea proceselor paralele (fork), sincronizare (waitpid) si comunicare (pipe).

stdio.h, stdlib.h, string.h: Pentru operatii standard de intrare/iesire si manipularea memoriei.

Standard Template Library (STL) din C++: Se utilizeaza vector, string, fstream si algorithm (pentru sortarea bulelor detectate in imagine).

6. Descrierea Codului Sursa
   Proiectul este impartit in trei componente logice majore, fiecare avand un rol bine definit:

Clientul (clientgrading.c)
Acesta este punctul de interactiune cu utilizatorul. Initializeaza un socket TCP si se conecteaza la adresa IP si portul serverului. Ruleaza o bucla infinita in care afiseaza un meniu simplu. Cand utilizatorul introduce calea catre o imagine, clientul impacheteaza acest text si il trimite serverului, blocandu-se apoi pana cand primeste nota finala sau un mesaj de eroare.

Serverul (servergrading.c)
Este inima aplicatiei. Ruleaza in mod continuu, asteptand conexiuni de la clienti prin intermediul functiilor bind, listen si accept. Cand primeste o cerere de corectare, apeleaza intai puntea OpenCV pentru a extrage raspunsurile din imagine.

Partea centrala de programare concurenta are loc aici: serverul creeaza un array de pipe-uri si apeleaza functia fork de 40 de ori (corespunzator celor 40 de intrebari). Fiecare proces fiu primeste un ID de intrebare, verifica prin functia C++ daca raspunsul studentului se potriveste cu baremul si scrie rezultatul (1 sau 0) in pipe-ul sau, dupa care executa exit. Procesul parinte citeste din toate pipe-urile, asteapta terminarea fiilor cu waitpid, calculeaza nota finala si trimite rezultatul inapoi clientului sub forma de text formatat manual.

Puntea OpenCV (opencv_punte.cpp)
Acest fisier face legatura intre codul C al serverului si biblioteca C++ OpenCV, expunand functiile prin directiva extern "C" pentru a putea fi apelate direct din servergrading.c.

Functia principala citeste intai fisierul barem.txt. Apoi, incarca imaginea testului in memorie, ii aplica un resize si o serie de filtre: o transforma in format grayscale, aplica un Gaussian blur pentru netezire si un Adaptive Threshold pentru a o transforma in alb-negru (binarizare), scotand in evidenta cerneala cu care a scris studentul. Imaginea este apoi taiata matematic in doua jumatati (coloana stanga si coloana dreapta).

Pentru fiecare coloana se cauta contururi. Se filtreaza doar contururile care au forma apropiata de un cerc si o anumita latime/inaltime. Aceste contururi sunt sortate intai pe axa Y (pe randuri) si apoi pe axa X (optiunile A, B, C, D). Pentru fiecare set de 4 bule, programul izoleaza interiorul fiecareia, numara pixelii care reprezinta marcajul si alege bula cu cei mai multi pixeli completati ca fiind raspunsul ales de student. Acest raspuns este salvat intr-un array global accesat ulterior de procesele fiu din server.

7. Compilare si Rulare
   Deoarece proiectul combina cod C si C++, procesul de compilare pentru server se realizeaza in mai multe etape. Fisierele sursa sunt intai transformate in fisiere obiect (.o), dupa care sunt legate intr-un singur executabil, adaugand referintele catre bibliotecile externe.

Dependente

sudo apt-get update

sudo apt-get install -y gcc g++ libopencv-dev pkg-config

Compilare

gcc -c servergrading.c -o servergrading.o

g++ -c opencv_punte.cpp -o opencv_punte.o $(pkg-config --cflags opencv4)

g++ servergrading.o opencv_punte.o -o server_grading $(pkg-config --libs opencv4)

gcc clientgrading.c -o client_grading

Pornirea procesului principal (Terminal 1):

./server

Serverul va afisa un mesaj de confirmare a pornirii pe portul specificat si va intra in starea de asteptare.

Pornirea interfetei client (Terminal 2):

./client

Dupa conectare, clientul va afisa meniul interactiv. Tastati cifra corespunzatoare actiunii dorite, apasati Enter, iar apoi introduceti calea catre fisierul imagine pentru a declansa procesul de corectare descris in capitolele anterioare.
