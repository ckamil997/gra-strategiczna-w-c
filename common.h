#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

// Klucze IPC 
#define KEY_MSG 1234
#define KEY_SHM 5678
#define KEY_SEM 9012

// Stałe gry 
#define MAX_PLAYERS 2
#define QUEUE_SIZE 10   // Rozmiar kolejki produkcji dla każdego gracza

// Typy jednostek
#define UNIT_LIGHT 0
#define UNIT_HEAVY 1
#define UNIT_CAVALRY 2
#define UNIT_WORKER 3

// Typy komunikatów (mtype) 
#define MSG_LOGIN 1     // Logowanie 
#define MSG_ACTION 2    // Akcja (klient -> serwer) 
#define MSG_STATE 20    // Stan gry (serwer -> klient) 

// Typy akcji (wewnątrz MSG_ACTION) 
#define ACTION_BUILD 1
#define ACTION_ATTACK 2

// Pojedyncze zadanie w kolejce produkcji 
typedef struct {
    int active;      // 1 jeśli slot zajęty, 0 jeśli wolny
    int unit_type;   // Typ jednostki (0-3)
    int count;       // Ilość sztuk
    int time_left;   // Pozostały czas w sekundach
} ProductionTask;

// Struktura ataku 
typedef struct {
    int active;         
    int attacker_id;
    int defender_id;
    int units[3];    // [0]=Lekka, [1]=Ciezka, [2]=Jazda 
    int time_left;
} AttackTask;

// Stan gracza (przesyłany do klienta do wyświetlenia) 
typedef struct {
    int pid;                // PID procesu (0 = brak gracza)
    int resources;          // Surowce
    int units[4];           // Ilość posiadanych jednostek
    int points;             // Punkty zwycięstwa
    ProductionTask production_queue[QUEUE_SIZE]; // Kolejka produkcji
    AttackTask incoming_attack; // Nadchodzący atak na tego gracza (tylko 1 na raz dla uproszczenia)
    char last_event[128];   // Ostatnie zdarzenie (komunikat)
    int last_event_timer;   // Czas wygasniecia komunikatu
    int game_over;          // Flaga końca gry (0=gra, 1=koniec)
    int shutdown_timer;     // Czas do automatycznego zamknięcia (0=brak odliczania)
} PlayerState;

// Główna struktura Pamięci Współdzielonej (Serwer) 
typedef struct {
    PlayerState players[MAX_PLAYERS];
    int game_active;
} GameSharedMemory;

// Struktura wiadomości w kolejce (Message Queue) 
typedef struct {
    long mtype;             // Typ komunikatu
    int player_id;          // ID gracza
    int action_type;        // ACTION_BUILD lub ACTION_ATTACK
    int data[5];            // Argumenty: data[0]..data[4] 
    PlayerState state_copy; // Kopia stanu do wyświetlenia (używane przy MSG_STATE)
} Message;

#endif