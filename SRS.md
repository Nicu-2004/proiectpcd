# Software Requirements Specification (SRS) - Grading App

##  Introducere
###  Scop
Acest document definește cerințele pentru **Grading App**, un sistem distribuit destinat automatizării corectării testelor grilă. Sistemul utilizează tehnologii de procesare a imaginilor (OpenCV) și execuție paralelă pentru a asigura o corectare rapidă și obiectivă.

###  Public Țintă
* Dezvoltatori de software.
* Personal administrativ educațional.
* Studenți/Utilizatori care doresc să înțeleagă funcționarea sistemului.

---

##  Descrierea Generală
###  Arhitectura Sistemului
Aplicația este construită pe un model **Client-Server** distribuit:
* **Client:** Interfață CLI pentru trimiterea imaginilor.
* **Server:** Motor de calcul scris în C, care gestionează conexiunile și procesele.
* **OpenCV Bridge:** Modul C++ pentru analiza vizuală a foilor de răspuns.

###  Fluxul Principal de Lucru
1. Clientul transmite comanda `grade:cale_imagine` prin protocol TCP.
2. Serverul primește cererea și încarcă imaginea prin modulul OpenCV.
3. Se aplică filtre de binarizare și detecție a contururilor pentru a identifica bulele completate.
4. Serverul execută `fork()` pentru a crea 40 de procese paralele (unul pentru fiecare întrebare).
5. Rezultatele sunt colectate prin **Pipes** și calculate într-o notă finală.

---

##  Cerințe Funcționale

| ID | Funcționalitate | Descriere Detaliată |
| :--- | :--- | :--- |
| **F1** | Conectivitate TCP | Serverul trebuie să asculte pe portul 9090 și să accepte conexiuni simultane. |
| **F2** | Procesare OMR | Identificarea automată a răspunsului ales dintr-un set de 4 bule (A, B, C, D). |
| **F3** | Evaluare Concurentă | Fiecare întrebare trebuie evaluată într-un proces fiu separat pentru maximizarea performanței. |
| **F4** | Validare Barem | Compararea răspunsurilor detectate cu datele din `barem.txt`. |
| **F5** | Calcul Automat | Returnarea notei finale în format zecimal (ex: 9.5 / 10). |

---

##  Cerințe Non-Funcționale
* **Performanță:** Timpul de procesare pentru un test de 40 de întrebări nu trebuie să depășească 2 secunde.
* **Fiabilitate:** Sistemul trebuie să notifice utilizatorul dacă imaginea este neclară sau nu poate fi citită.
* **Extensibilitate:** Arhitectura permite adăugarea ulterioară a suportului pentru răspunsuri multiple.

---

##  Specificații Tehnice

###  Stiva Tehnologică
* **Limbaje:** C (Networking & Concurrency), C++ (OpenCV).
* **Biblioteci:** OpenCV 4.x, POSIX Sockets, sys/wait.h.
* **Protocol:** TCP/IP (Mesaje text brute).

###  Modelul de Date Intern
* `raspunsuri_student[40]`: Array de tip integer populat de modulul vizual.
* `barem_corect[40]`: Array încărcat din fișierul de configurare.

---

##  Instrucțiuni de Compilare și Rulare

**Comenzi compilare**
### Dependențe
```bash
sudo apt-get install gcc g++ libopencv-dev pkg-config

# Compilare Server (Linker C++ pentru OpenCV)
g++ -c opencv_punte.cpp -o opencv_punte.o $(pkg-config --cflags opencv4)
gcc -c servergrading.c -o servergrading.o
g++ servergrading.o opencv_punte.o -o server_grading $(pkg-config --libs opencv4)

# Compilare Client
gcc clientgrading.c -o client_grading
