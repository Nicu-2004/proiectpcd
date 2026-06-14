# Grading App - Motor OMR Asincron

[![Language](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B-00599C?style=flat&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B)
[![Library](https://img.shields.io/badge/Library-OpenCV-5C3EE8?style=flat&logo=opencv)](https://opencv.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux-FCC624?style=flat&logo=linux&logoColor=black)](https://www.linux.org/)

**Grading App** este o aplicație distribuită de tip **Client-Server**, dezvoltată nativ în C (POSIX). Scopul principal al sistemului este automatizarea procesului de corectare a testelor grilă prin tehnologii de recunoaștere optică a marcajelor (**OMR - Optical Mark Recognition**).

Elementul arhitectural central este abordarea de tip **Microserviciu Stateless (Agnostic)**. Aplicația folosește o arhitectură concurentă avansată (Multithreading + Asynchronous I/O) pentru a procesa evaluările rapid, gestionând eficient resursele procesorului și memoriei.

---

## 1. Arhitectura Sistemului

Sistemul renunță la procesele clasice (`fork`) în favoarea unei arhitecturi **Multithreaded**, utilizând multiplexare și un protocol binar personalizat (Custom Application Protocol). Serverul este complet independent (stateless) – nu stochează bareme local, ci primește dinamic condițiile de evaluare direct prin rețea de la clienți.

### Cele 4 Fire de Execuție (Threads) ale Serverului:
* **📡 INET Thread (Rețea):** Folosește apelul de sistem `poll()` pentru a asigura I/O asincron, ascultând simultan până la 64 de clienți fără blocaje. Construiește job-urile și le adaugă în coada FIFO, protejând accesul cu un `pthread_mutex`.
* **🧠 Worker Thread (Procesare Optică):** Consumatorul. Stă adormit (`0% CPU`) folosind variabile de condiție (`pthread_cond_wait`). Este trezit de INET Thread, preia imaginea și baremul, apelează biblioteca OpenCV pentru analiză, eliberează rapid Mutex-ul și comunică bidirecțional cu clientul.
* **🛡️ Admin Thread (Securitate):** Ascultă pe un Socket UNIX local (`/tmp/omr_admin.sock`). Gestionează o conexiune 1:1 exclusivă prin mecanisme de Handshake (`OK`/`BUSY`) pentru comenzi de telemetrie și oprire de urgență.
* **👀 INotify Thread (Monitorizare):** Interacționează direct cu nucleul Linux folosind API-ul `inotify` pentru a loga în consolă crearea și modificarea fișierelor temporare pe `/tmp`.

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

* **Standardul C (POSIX 2008):** Sockets (`sys/socket.h`), I/O Asincron (`poll.h`), Concurrency (`pthread.h`), Linux Kernel API (`sys/inotify.h`).
* **C++ & OpenCV 4.6:** `opencv_punte.cpp` acționează ca wrapper, expunând metode `extern "C"`. Utilizează funcții de computer vision: conversie Grayscale, Gaussian Blur, Adaptive Thresholding, `findContours` și algoritmi matematici pentru determinarea opțiunii bifate pe baza numărului de pixeli întunecați.

---

## 6. Compilare și Rulare

Pentru a construi arhitectura hibridă C/C++, se utilizează flag-ul `-pthread` și biblioteca OpenCV 4.6.

### Compilarea Modulelor
```bash
# 1. Compilarea Serverului (Creierul operațiunii) cu Puntea OpenCV
gcc servergrading.c omr_punte.c -o server -pthread -I/home/luky931/ocvWrapper46/source -L/usr/lib -locvCPPWrapper46

# 2. Compilarea Clientului (Utilizatorul)
gcc clientgrading.c -o client

# 3. Compilarea Aplicației de Management (Admin)
gcc admingrading.c -o admin
