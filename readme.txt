README - Tema 2 Protocoale de Comunicatii 2025

Descriere:
Aceasta tema implementeaza o aplicatie client-server pentru gestionarea mesajelor folosind protocoalele TCP si UDP.

Serverul actioneaza ca un broker de mesaje, intermediar intre clienti TCP (abonare, dezabonare) si clienti UDP (publicare mesaje).

Fisiere:

* server.c — implementarea completa a serverului:

  * asculta conexiuni TCP si UDP pe acelasi port
  * gestioneaza abonati si topicuri

* subscriber.c — implementarea completa a clientului TCP:

  * se conecteaza la server cu un ID unic
  * trimite comenzi subscribe, unsubscribe, exit
  * primeste si afiseaza mesajele de la server

* Makefile — permite compilarea simpla:
  make          # compileaza server si subscriber
  make clean    # sterge fisierele binare

Rulare:

1. Pornire server:
   ./server <PORT>

2. Pornire subscriber:
   ./subscriber \<ID\_CLIENT> \<IP\_SERVER> <PORT>
   Exemplu:
   ./subscriber C1 127.0.0.1 12345

Protocol de aplicatie (TCP):
Pentru a delimita mesajele pe conexiunile TCP, se foloseste un framing:

* fiecare mesaj incepe cu un antet de 2 octeti (big-endian) care specifica lungimea mesajului
* apoi urmeaza continutul mesajului (comanda sau raspuns)

Aceasta abordare asigura:

* decuplarea completa de segmentarea TCP
* procesare robusta in caz de recv() cu trunchiere/concatenare

Functiile folosite sunt:

* send_framed() — trimite cu antet
* recv_framed() — citeste mesaj complet, indiferent de cate apeluri recv() sunt necesare

Detalii:

* Nagle dezactivat (TCP_NODELAY) pentru latenta mica
* select() pentru multiplexare
* Nu am implementat si partea de wildcarduri intrucat am incercat, dar nu mi-a iesit si nu am mai avut timp
* Serverul este complet functional, doar partea de matching cu wildcarduri lipseste din implementare

Observatii:

* Mesajele afisate sunt exact cele din enunt
* Nu se fac printuri suplimentare (doar cele cerute)
* Fiecare mesaj primit e afisat imediat la client
* Comenzile exit inchid clientul sau serverul
* ID-ul clientului este verificat pentru unicitate si persista intre reconectari