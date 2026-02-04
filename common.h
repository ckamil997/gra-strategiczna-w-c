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

// --- STRUKTURY ---

typedef struct {
    int active, unit_type, count, time_left;
} ProductionTask;

typedef struct {
    int active, attacker_id, defender_id, units[3], time_left;
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
    int player_id, action_type, data[5];
    PlayerState state_copy;
} Message;

// --- FUNKCJE IPC ---

// Helper do sprawdzania błędów przy pobieraniu ID (dla shm, msg, sem)
static inline int sys_check(int res, int flags, const char *msg) {
    if (res == -1 && (flags & IPC_CREAT)) { perror(msg); exit(1); }
    return res;
}

// 1. Kolejki
static inline int get_message_queue_id(key_t k, int f) { return sys_check(msgget(k, f), f, "msgget"); }
static inline void remove_message_queue(int id) { msgctl(id, IPC_RMID, NULL); }

static inline void send_message(int id, void *msg, size_t sz, int f) {
    if (msgsnd(id, msg, sz, f) == -1 && !(f & IPC_NOWAIT)) perror("msgsnd");
}

static inline int receive_message(int id, void *msg, size_t sz, long type, int f) {
    return msgrcv(id, msg, sz, type, f);
}

// 2. Pamięć
static inline int get_shared_memory_id(key_t k, size_t s, int f) { return sys_check(shmget(k, s, f), f, "shmget"); }
static inline void remove_shared_memory(int id) { shmctl(id, IPC_RMID, NULL); }

static inline void *attach_shared_memory(int id, int f) {
    void *ptr = shmat(id, NULL, f);
    if (ptr == (void *)-1) { perror("shmat"); exit(1); }
    return ptr;
}
static inline void detach_shared_memory(const void *addr) { shmdt(addr); }

// 3. Semafory
static inline int get_semaphore_id(key_t k, int n, int f) { return sys_check(semget(k, n, f), f, "semget"); }
static inline void remove_semaphore(int id) { semctl(id, 0, IPC_RMID, NULL); }
static inline void initialize_semaphore(int id, int num, int val) { semctl(id, num, SETVAL, val); }

static inline void semaphore_op(int id, int op) {
    struct sembuf b = {0, op, 0};
    if (semop(id, &b, 1) == -1) perror("semop");
}

// Makra dla czytelności
#define semaphore_lock(id) semaphore_op(id, -1)
#define semaphore_unlock(id) semaphore_op(id, 1)

#endif