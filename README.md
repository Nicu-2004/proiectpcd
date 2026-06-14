# Grading App - Motor OMR Asincron

[![Language](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B-00599C?style=flat&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Library](https://img.shields.io/badge/Library-OpenCV-5C3EE8?style=flat&logo=opencv)](https://opencv.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux-FCC624?style=flat&logo=linux&logoColor=black)](https://www.linux.org/)

**Grading App** este o aplicație distribuită de tip **Client-Server**, dezvoltată nativ în C (POSIX). Scopul principal al sistemului este automatizarea procesului de corectare a testelor grilă prin tehnologii de recunoaștere optică a marcajelor (**OMR - Optical Mark Recognition**).

Elementul arhitectural central este abordarea de tip **Microserviciu Stateless (Agnostic)**. Aplicația folosește o arhitectură concurentă avansată (Multithreading + Asynchronous I/O) pentru a procesa evaluările rapid, gestionând eficient resursele procesorului și memoriei.

---

## 1. Arhitectura Sistemului

Folosirea unei arhitecturi **Multithreaded**, utilizând multiplexare și un protocol binar personalizat (Custom Application Protocol). Serverul este complet independent (stateless) – nu stochează bareme local, ci primește dinamic condițiile de evaluare direct prin rețea de la clienți.

### Cele 4 Fire de Execuție (Threads) ale Serverului:
* ** INET Thread (Rețea):** Folosește apelul de sistem `poll()` pentru a asigura I/O asincron, ascultând simultan până la 64 de clienți fără blocaje. Construiește job-urile și le adaugă în coada FIFO, protejând accesul cu un `pthread_mutex`.
* ** Worker Thread (Procesare Optică):** Consumatorul. Stă adormit (`0% CPU`) folosind variabile de condiție (`pthread_cond_wait`). Este trezit de INET Thread, preia imaginea și baremul, apelează biblioteca OpenCV pentru analiză, eliberează rapid Mutex-ul și comunică bidirecțional cu clientul.
* ** Admin Thread (Securitate):** Ascultă pe un Socket UNIX local (`/tmp/omr_admin.sock`). Gestionează o conexiune 1:1 exclusivă prin mecanisme de Handshake (`OK`/`BUSY`) pentru comenzi de telemetrie și oprire de urgență.
* ** INotify Thread (Monitorizare):** Interacționează direct cu nucleul Linux folosind API-ul `inotify` pentru a loga în consolă crearea și modificarea fișierelor temporare pe `/tmp`.

---

## 2. Fluxul de Date și Sincronizarea

1. **Clientul TCP** deschide sesiunea și trimite un pachet binar complex conținând "Magic Number", dimensiunea fișierului, **Baremul de corectare** și fluxul imaginii necorectate (`sendfile`).
2. **INET Thread** interceptează conexiunea prin `poll()`, descarcă fișierul temporar și blochează (`Lock`) coada FIFO circulară folosind Mutex pentru a introduce job-ul.
3. INET Thread eliberează Mutex-ul (`Unlock`) și emite un semnal (`pthread_cond_signal`) pentru a trezi Worker-ul.
4. **Worker Thread** extrage pachetul (Imagine + Barem specific), deblochează instant coada pentru a lăsa rețeaua să primească alți clienți, și execută evaluarea prin puntea **OpenCV C++**.
5. Serverul trimite rezultatul bidirecțional: un bloc text fix de 256 de octeți (nota calculată) urmat de **fluxul binar al imaginii prelucrate** (pătratele recunoscute desenate pe test).
6. Serverul curăță memoria și șterge fișierele temporare (`unlink`).

---

## 3. Protocolul Binar Custom

Aplicația renunță la comunicarea prin text brut și folosește un protocol binar eficient, cu o arhitectură Connection-per-Request.

### Request-ul (Client ➔ Server)
Definit prin structura `app_header_t`:
* `Magic Bytes`: `"OMR"` (pentru validarea conexiunii)
* `File Size`: 4 octeți (`uint32_t` în Network Byte Order - `htonl`)
* `Barem`: 40 de caractere reprezentând baremul testului curent.
* **Payload**: Fluxul binar al imaginii transmise secvențial (blocuri de 4096 bytes).

### Response-ul Bidirecțional (Server ➔ Client)
* `Text Header`: 256 octeți constanți (ex. *"Succes! Nota acordata: 9.75 / 10"*).
* `Image Size`: 4 octeți (`uint32_t`) reprezentând dimensiunea pozei cu rezultatul corectat.
* **Payload**: Fluxul binar al imaginii procesate.

---

## 4. Panoul Administratorului (AdminApp)

Pentru mentenanță, sistemul include un client specializat (`admingrading.c`) care interoghează Serverul folosind un Socket UNIX (comunicare inter-proces locală), oferind un nivel maxim de securitate. 

**Funcționalități Admin:**
* Acces strict de tip 1:1. Dacă un admin este conectat, sistemul va refuza orice altă conexiune administrativă (`Handshake: BUSY`).
* `report:clients` - Număr clienți INET activi asincron.
* `report:queue` - Statusul Ring Buffer-ului (FIFO) și sarcini în așteptare.
* `report:success` - Rata globală de succes a analizei OpenCV.
* `report:time` - Timpul mediu de execuție/procesare per evaluare.
* **Graceful Shutdown (`0`):** Trimite un semnal `pthread_cond_broadcast` pentru a trezi și opri ordonat toate firele de execuție, curățând memoria și evitând memory leaks.

---

## 5. Tehnologii și Biblioteci Utilizate

* **Standardul C (POSIX 2008):** Baza întregii aplicații. Sockets (`sys/socket.h`), I/O Asincron (`poll.h`), Concurrency (`pthread.h`), și API-ul de Kernel Linux (`sys/inotify.h`).
* **Wrapper Precompilat OpenCV (C/C++):** Interacțiunea cu motorul OpenCV 4.6 nu se face prin recompilarea surselor C++ la fiecare rulare, ci prin link-area unei biblioteci externe precompilate (`libocvCPPWrapper46`). Fișierul de legătură `omr_punte.c` expune metodele de evaluare (`incarca_imagine_opencv`, `proceseaza_intrebare_opencv`) direct în C folosind structuri opace (`struct Mat_t`). Acest design asigură o separare totală și curată între logica serverului (C pur) și procesarea optică (C++), care execută binarizarea (Adaptive Thresholding) și algoritmii de detecție a contururilor (`findContours`).

---

## 6. Compilare și Rulare

Compilarea se face exclusiv prin compilatorul de C (`gcc`), utilizând flag-ul `-pthread` pentru multithreading și link-ând direct biblioteca externă precompilată (`libocvCPPWrapper46`) pentru funcțiile OpenCV.

### Compilarea Executabilelor
```bash
# 1. Compilarea Serverului (Creierul operațiunii) 
# Se compilează fișierele C și se face linking cu librăria OpenCV Wrapper
gcc servergrading.c omr_punte.c -o server -pthread -I~/ocvWrapper46/source -L/usr/lib -locvCPPWrapper46

# 2. Compilarea Clientului (Interfața de evaluare)
gcc clientgrading.c -o client

# 3. Compilarea Aplicației de Management (Panoul Admin)
gcc admingrading.c -o admin
