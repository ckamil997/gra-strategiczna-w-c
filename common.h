#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Klucze IPC 
#define KEY_MSG 1234
#define KEY_SHM 5678
#define KEY_SEM 9012

// Stałe gry 
#define MAX_PLAYERS 2
#define QUEUE_SIZE 10
#define POINTS_TO_WIN 5
#define START_RESOURCES 300
#define BASE_INCOME 50
#define WORKER_INCOME 5
#define ATTACK_TRAVEL_TIME 5

// Statystyki jednostek
static const int UNIT_COST[]      = {100, 250, 550, 150};
static const float UNIT_ATTACK[]  = {1.0f, 1.5f, 3.5f, 0.0f};
static const float UNIT_DEFENSE[] = {1.2f, 3.0f, 1.2f, 0.0f};
static const int UNIT_TIME[]      = {2, 3, 5, 2};

// Makro pomocnicze
#define my_print(s) write(1, (s), strlen(s))

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

// --- STRUKTURY ---

typedef struct {
    int active, unit_type, count, time_left;
} ProductionTask;

typedef struct {
    int active, attacker_id, units[3], time_left;
} AttackTask;

// Stan gracza (przesyłany do klienta do wyświetlenia) 
typedef struct {
    int pid, resources, units[4], points;
    ProductionTask production_queue[QUEUE_SIZE];
    AttackTask incoming_attack;
    char last_event[128];
    int last_event_timer, game_over, shutdown_timer;
} PlayerState;

typedef struct {
    PlayerState players[MAX_PLAYERS];
    int game_active;
} GameSharedMemory;

typedef struct {
    long mtype;
    int player_id, action_type, data[3];
    PlayerState state_copy; 
} Message;

// Makra do obsługi semaforów
#define sem_lock(id) semop(id, &(struct sembuf){0, -1, 0}, 1)
#define sem_unlock(id) semop(id, &(struct sembuf){0, 1, 0}, 1)

#endif