#include <unistd.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include "common.h"

// Index: 0=Lekka, 1=Ciezka, 2=Jazda, 3=Robotnicy 
const int UNIT_COST[]    = {100, 250, 550, 150};
const float UNIT_ATTACK[]  = {1.0, 1.5, 3.5, 0.0};
const float UNIT_DEFENSE[] = {1.2, 3.0, 1.2, 0.0};
const int UNIT_TIME[]    = {2, 3, 5, 2};

// Funkcje pomocnicze
void my_print(const char *str) {
    write(1, str, strlen(str));
}

void my_print_int(int num) {
    char buf[20];
    sprintf(buf, "%d", num);
    write(1, buf, strlen(buf));
}

void sem_op(int semid, int op) {
    struct sembuf buf = {0, op, 0};
    if (semop(semid, &buf, 1) == -1) my_print("Blad Sem\n");
}
#define sem_p(id) sem_op(id, -1)
#define sem_v(id) sem_op(id, 1)

// --- LOGIKA GRY ---

// Funkcja pomocnicza do obliczania siły ataku/obrony
float calculate_strength(int *units, const float *multipliers, int count) {
    float sum = 0;
    for(int i=0; i<count; i++) sum += units[i] * multipliers[i];
    return sum;
}

// Funkcja odpowiedzialna za rozstrzyganie walki
void resolve_combat(GameSharedMemory *shm, int defender_id) {
    if (!shm->players[defender_id].incoming_attack.active) return;
    
    int attacker_id = shm->players[defender_id].incoming_attack.attacker_id;
    int *att_units = shm->players[defender_id].incoming_attack.units; 
    
    // Oblicz siły
    float SA = calculate_strength(att_units, UNIT_ATTACK, 3);
    float SB = calculate_strength(shm->players[defender_id].units, UNIT_DEFENSE, 4);
    if (SB < 0.1) SB = 0.1;

    float SB_attack_power = calculate_strength(shm->players[defender_id].units, UNIT_ATTACK, 4);

    // Rozstrzygniecie bitwy
    if (SA > SB) {
        // Atakujący wygrywa
        for (int i = 0; i < 4; i++) shm->players[defender_id].units[i] = 0;
        shm->players[attacker_id].points++;
        shm->players[defender_id].game_over = (shm->players[attacker_id].points >= 5);
        shm->players[attacker_id].game_over = (shm->players[attacker_id].points >= 5);
        
        sprintf(shm->players[attacker_id].last_event, "Bitwa WYGRANA! +1 Pkt");
        sprintf(shm->players[defender_id].last_event, "Bitwa PRZEGRANA! Straciles armie.");
    } else {
        // Obrońca się broni
        for (int i = 0; i < 4; i++) {
            int current = shm->players[defender_id].units[i];
            int loss = (int)(current * SA / SB);
            shm->players[defender_id].units[i] = (current > loss) ? current - loss : 0;
        }
        sprintf(shm->players[defender_id].last_event, "Obrona skuteczna! Poniesiono straty.");
        sprintf(shm->players[attacker_id].last_event, "Atak odparty.");
    }
    shm->players[attacker_id].last_event_timer = 5;
    shm->players[defender_id].last_event_timer = 5;

    // Straty atakujacego (kontratak obrońcy)
    float SA_defense = calculate_strength(att_units, UNIT_DEFENSE, 3);
    if (SA_defense < 0.1) SA_defense = 0.1;

    float loss_ratio = (SB_attack_power > SA_defense) ? 1.0 : (SB_attack_power / SA_defense);
    
    for(int i=0; i<3; i++) {
        int loss = (int)(att_units[i] * loss_ratio);
        int surviving = att_units[i] - loss;
        if (surviving < 0) surviving = 0;
        shm->players[attacker_id].units[i] += surviving;
    }

    shm->players[defender_id].incoming_attack.active = 0;

    // Warunek konca gry
    if (shm->players[attacker_id].points >= 5) {
        shm->game_active = 0;
        shm->players[attacker_id].shutdown_timer = 5;
        shm->players[defender_id].shutdown_timer = 5;
        sprintf(shm->players[attacker_id].last_event, "GRATULACJE! Wygrales!");
        sprintf(shm->players[defender_id].last_event, "PRZEGRALES.");
    }
}

// Proces odpowiedzialny za całą logikę gry (surowce, produkcja, bitwy)
void run_game_logic_process(int semid, GameSharedMemory *shm) {
    while (1) {
        sem_p(semid);
        
        // Obsługa zamykania gry (automatyczne wyłączenie po 10s od końca)
        if (shm->players[0].game_over && !shm->game_active) {
            static int ticks = 0;
            if (++ticks >= 10) { // Co ok 1s
                ticks = 0;
                if (shm->players[0].shutdown_timer-- <= 0) {
                    sem_v(semid);
                    kill(getppid(), SIGTERM);
                    exit(0);
                }
                for(int i=1; i<MAX_PLAYERS; i++) shm->players[i].shutdown_timer = shm->players[0].shutdown_timer;
            }
            sem_v(semid);
            usleep(100000);
            continue;
        }
        
        if (!shm->game_active) { sem_v(semid); sleep(1); continue; }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (shm->players[i].pid == 0) continue;

            // Surowce: Worker * 5 + Baza(50)
            shm->players[i].resources += 50 + (shm->players[i].units[UNIT_WORKER] * 5);

            // Ataki
            if(shm->players[i].incoming_attack.active) {
                if(--shm->players[i].incoming_attack.time_left <= 0) resolve_combat(shm, i);
            }

            // Wiadomości - wygaszanie
            if(shm->players[i].last_event_timer > 0) {
                if(--shm->players[i].last_event_timer <= 0) shm->players[i].last_event[0] = '\0';
            }

            // Produkcja
            for (int q = 0; q < QUEUE_SIZE; q++) {
                if (shm->players[i].production_queue[q].active) {
                    if (--shm->players[i].production_queue[q].time_left <= 0) {
                        int type = shm->players[i].production_queue[q].unit_type;
                        shm->players[i].units[type]++;
                        shm->players[i].production_queue[q].count--;
                        
                        if (shm->players[i].production_queue[q].count > 0) {
                             shm->players[i].production_queue[q].time_left = UNIT_TIME[type];
                        } else {
                            shm->players[i].production_queue[q].active = 0;
                        }
                    }
                    break; // Kolejka FIFO - tylko pierwszy element
                }
            }
        }
        sem_v(semid);
        sleep(1);
    }
}

// --- KOMUNIKACJA ---

// Funkcja przetwarzająca akcje żądane przez klienta (Budowa/Atak)
void handle_client_action(GameSharedMemory *shm, Message *msg) {
    int p_id = msg->player_id;
    if (p_id < 0 || p_id >= MAX_PLAYERS || shm->players[p_id].pid == 0) return;

    if (msg->action_type == ACTION_BUILD) {
        int type = msg->data[0];
        int count = msg->data[1];
        int cost = UNIT_COST[type] * count;

        if (type >= 0 && type <= 3 && count > 0 && shm->players[p_id].resources >= cost) {
            for(int k=0; k<QUEUE_SIZE; k++) {
                if(!shm->players[p_id].production_queue[k].active) {
                    shm->players[p_id].production_queue[k].active = 1;
                    shm->players[p_id].production_queue[k].unit_type = type;
                    shm->players[p_id].production_queue[k].count = count;
                    shm->players[p_id].production_queue[k].time_left = UNIT_TIME[type];
                    shm->players[p_id].resources -= cost;
                    break;
                }
            }
        }
    } 
    else if (msg->action_type == ACTION_ATTACK) {
        int u[3] = {msg->data[0], msg->data[1], msg->data[2]};
        int def_id = (p_id == 0) ? 1 : 0;
        
        // Czy gracz ma tyle jednostek?
        if (shm->players[p_id].units[0] >= u[0] && 
            shm->players[p_id].units[1] >= u[1] &&
            shm->players[p_id].units[2] >= u[2] &&
            (u[0]+u[1]+u[2]) > 0 && 
            !shm->players[def_id].incoming_attack.active) 
        {
            shm->players[def_id].incoming_attack.active = 1;
            shm->players[def_id].incoming_attack.attacker_id = p_id;
            shm->players[def_id].incoming_attack.defender_id = def_id;
            shm->players[def_id].incoming_attack.time_left = 5;
            memcpy(shm->players[def_id].incoming_attack.units, u, sizeof(u));
            
            shm->players[p_id].units[0] -= u[0];
            shm->players[p_id].units[1] -= u[1];
            shm->players[p_id].units[2] -= u[2];

            sprintf(shm->players[p_id].last_event, "Atak wyslany (5s)");
            shm->players[p_id].last_event_timer = 5;
            sprintf(shm->players[def_id].last_event, "ALARM! Nadchodzi atak!");
            shm->players[def_id].last_event_timer = 5;
        }
    }
}

// Proces odpowiedzialny za komunikację z klientami poprzez kolejkę komunikatów
void run_communication_process(int msgid, int semid, GameSharedMemory *shm) {
    Message msg;
    int connected = 0;

    while (1) {
        // 1. Odbiór wiadomości (z flagą IPC_NOWAIT aby nie blokować pętli)
        if (msgrcv(msgid, &msg, sizeof(Message) - sizeof(long), -10, IPC_NOWAIT) != -1) {
            sem_p(semid);
            
            if (msg.mtype == MSG_LOGIN && connected < MAX_PLAYERS) {
                shm->players[connected].pid = msg.player_id;
                my_print("Gracz dolaczyl: "); my_print_int(connected); my_print("\n");
                
                // Odpowiedz
                Message resp = {.mtype = msg.player_id, .player_id = connected};
                msgsnd(msgid, &resp, sizeof(Message) - sizeof(long), 0);
                
                connected++;
                if (connected == MAX_PLAYERS) {
                    shm->game_active = 1;
                    my_print("Start Gry!\n");
                }
            }
            else if (msg.mtype == MSG_ACTION && shm->game_active) {
                handle_client_action(shm, &msg);
            }
            sem_v(semid);
        }

        // 2. Wysyłanie stanu gry
        sem_p(semid);
        if (connected == MAX_PLAYERS) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (shm->players[i].pid != 0) {
                    Message update = {.mtype = MSG_STATE + i, .state_copy = shm->players[i]};
                    msgsnd(msgid, &update, sizeof(Message) - sizeof(long), 0);
                }
            }
        }
        sem_v(semid);
        usleep(200000); // 200ms
    }
}

int main() {
    my_print("Serwer Start...\n");

    // Czyszczenie starych zasobów IPC przed startem
    msgctl(msgget(KEY_MSG, 0666), IPC_RMID, NULL);
    shmctl(shmget(KEY_SHM, 0, 0666), IPC_RMID, NULL);
    semctl(semget(KEY_SEM, 0, 0666), 0, IPC_RMID, NULL);

    int msgid = msgget(KEY_MSG, IPC_CREAT | 0666);
    int shmid = shmget(KEY_SHM, sizeof(GameSharedMemory), IPC_CREAT | 0666);
    int semid = semget(KEY_SEM, 1, IPC_CREAT | 0666);
    
    if (msgid == -1 || shmid == -1 || semid == -1) { perror("IPC Error"); exit(1); }

    // Inicjalizacja pamięci współdzielonej i semafora
    GameSharedMemory *shm = (GameSharedMemory *)shmat(shmid, NULL, 0);
    semctl(semid, 0, SETVAL, 1);
    memset(shm, 0, sizeof(GameSharedMemory));
    shm->players[0].resources = 300;
    shm->players[1].resources = 300;

    // Rozwidlenie procesów
    if (fork() == 0) {
        run_game_logic_process(semid, shm);
    } else {
        run_communication_process(msgid, semid, shm);
    }
    return 0;
}
