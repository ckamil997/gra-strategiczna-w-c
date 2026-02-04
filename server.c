#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include "common.h"

// --- LOGIKA GRY ---

// Funkcja pomocnicza do obliczania siły ataku/obrony
float calculate_strength(int *units, const float *multipliers, int count) {
    float sum = 0;
    for(int i=0; i<count; i++) sum += units[i] * multipliers[i];
    return sum;
}

// Funkcja odpowiedzialna za rozstrzyganie walki
void resolve_combat(GameSharedMemory *state, int def_id) {
    PlayerState *def = &state->players[def_id];
    if (!def->incoming_attack.active) return;
    
    int atk_id = def->incoming_attack.attacker_id;
    PlayerState *atk = &state->players[atk_id];
    int *atk_units = def->incoming_attack.units; 
    
    // Oblicz siły
    float attack_str = calculate_strength(atk_units, UNIT_ATTACK, 3);
    float defense_str = calculate_strength(def->units, UNIT_DEFENSE, 4);
    if (defense_str < 0.1f) defense_str = 0.1f;

    float def_attack_power = calculate_strength(def->units, UNIT_ATTACK, 4);

    // Rozstrzygniecie bitwy
    if (attack_str > defense_str) {
        for (int i = 0; i < 4; i++) def->units[i] = 0;
        atk->points++;
        def->game_over = atk->game_over = (atk->points >= POINTS_TO_WIN);
        
        sprintf(atk->last_event, "Bitwa WYGRANA! +1 Pkt");
        sprintf(def->last_event, "Bitwa PRZEGRANA! Straciles armie.");
    } else {
        for (int i = 0; i < 4; i++) {
            int loss = (int)(def->units[i] * attack_str / defense_str);
            def->units[i] = (def->units[i] > loss) ? def->units[i] - loss : 0;
        }
        sprintf(def->last_event, "Obrona skuteczna! Poniesiono straty.");
        sprintf(atk->last_event, "Atak odparty.");
    }
    atk->last_event_timer = def->last_event_timer = 5;

    // Straty atakujacego (kontratak obrońcy)
    float atk_defense = calculate_strength(atk_units, UNIT_DEFENSE, 3);
    if (atk_defense < 0.1f) atk_defense = 0.1f;
    float loss_ratio = (def_attack_power > atk_defense) ? 1.0f : (def_attack_power / atk_defense);
    
    for (int i = 0; i < 3; i++) {
        int surviving = atk_units[i] - (int)(atk_units[i] * loss_ratio);
        atk->units[i] += (surviving > 0) ? surviving : 0;
    }

    def->incoming_attack.active = 0;

    // Warunek konca gry
    if (atk->points >= POINTS_TO_WIN) {
        state->game_active = 0;
        atk->shutdown_timer = def->shutdown_timer = 5;
        sprintf(atk->last_event, "GRATULACJE! Wygrales!");
        sprintf(def->last_event, "PRZEGRALES.");
    }
}

// Proces odpowiedzialny za całą logikę gry (surowce, produkcja, bitwy)
void run_game_logic_process(int semaphore_id, GameSharedMemory *state) {
    int ticks = 0;
    while (1) {
        sem_lock(semaphore_id);
        
        // Obsługa zamykania gry (automatyczne wyłączenie po 5s)
        if (state->players[0].game_over && !state->game_active) {
            if (++ticks >= 10) {
                ticks = 0;
                if (state->players[0].shutdown_timer-- <= 0) {
                    sem_unlock(semaphore_id);
                    kill(getppid(), SIGTERM);
                    exit(0);
                }
                for (int i = 1; i < MAX_PLAYERS; i++)
                    state->players[i].shutdown_timer = state->players[0].shutdown_timer;
            }
            sem_unlock(semaphore_id);
            usleep(100000);
            continue;
        }
        
        if (!state->game_active) { 
            sem_unlock(semaphore_id); 
            sleep(1); 
            continue; 
        }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            PlayerState *player = &state->players[i];
            if (player->pid == 0) continue;

            // Surowce
            player->resources += BASE_INCOME + (player->units[UNIT_WORKER] * WORKER_INCOME);

            // Ataki
            if (player->incoming_attack.active && --player->incoming_attack.time_left <= 0)
                resolve_combat(state, i);

            // Wiadomości - wygaszanie
            if (player->last_event_timer > 0 && --player->last_event_timer <= 0)
                player->last_event[0] = '\0';

            // Produkcja
            for (int q = 0; q < QUEUE_SIZE; q++) {
                ProductionTask *task = &player->production_queue[q];
                if (task->active) {
                    if (--task->time_left <= 0) {
                        player->units[task->unit_type]++;
                        if (--task->count > 0)
                            task->time_left = UNIT_TIME[task->unit_type];
                        else
                            task->active = 0;
                    }
                    break;
                }
            }
        }
        sem_unlock(semaphore_id);
        sleep(1);
    }
}

// --- KOMUNIKACJA ---

// Funkcja przetwarzająca akcje żądane przez klienta (Budowa/Atak)
void handle_client_action(GameSharedMemory *state, Message *msg) {
    int pid = msg->player_id;
    if (pid < 0 || pid >= MAX_PLAYERS || state->players[pid].pid == 0) return;
    PlayerState *player = &state->players[pid];

    if (msg->action_type == ACTION_BUILD) {
        int type = msg->data[0], count = msg->data[1];
        int cost = UNIT_COST[type] * count;

        if (type >= 0 && type <= 3 && count > 0 && player->resources >= cost) {
            for (int q = 0; q < QUEUE_SIZE; q++) {
                ProductionTask *task = &player->production_queue[q];
                if (!task->active) {
                    task->active = 1;
                    task->unit_type = type;
                    task->count = count;
                    task->time_left = UNIT_TIME[type];
                    player->resources -= cost;
                    break;
                }
            }
        }
    } 
    else if (msg->action_type == ACTION_ATTACK) {
        int atk_units[3] = {msg->data[0], msg->data[1], msg->data[2]};
        int def_id = (pid == 0) ? 1 : 0;
        PlayerState *def = &state->players[def_id];
        
        int valid = 1;
        for (int j = 0; j < 3 && valid; j++)
            valid = (player->units[j] >= atk_units[j]);
        
        if (valid && (atk_units[0] + atk_units[1] + atk_units[2]) > 0 && !def->incoming_attack.active) {
            def->incoming_attack.active = 1;
            def->incoming_attack.attacker_id = pid;
            def->incoming_attack.time_left = ATTACK_TRAVEL_TIME;
            memcpy(def->incoming_attack.units, atk_units, sizeof(atk_units));
            
            for (int j = 0; j < 3; j++)
                player->units[j] -= atk_units[j];

            sprintf(player->last_event, "Atak wyslany (%ds)", ATTACK_TRAVEL_TIME);
            player->last_event_timer = ATTACK_TRAVEL_TIME;
            sprintf(def->last_event, "ALARM! Nadchodzi atak!");
            def->last_event_timer = ATTACK_TRAVEL_TIME;
        }
    }
}

// Proces odpowiedzialny za komunikację z klientami poprzez kolejkę komunikatów
void run_communication_process(int message_queue_id, int semaphore_id, GameSharedMemory *shared_game_state) {
    Message message;
    size_t msg_size = sizeof(Message) - sizeof(long);
    int connected_players_count = 0;

    while (1) {
        // 1. Odbiór wiadomości (z flagą IPC_NOWAIT aby nie blokować pętli)
        if (msgrcv(message_queue_id, &message, msg_size, -10, IPC_NOWAIT) != -1) {
            sem_lock(semaphore_id);
            
            if (message.mtype == MSG_LOGIN && connected_players_count < MAX_PLAYERS) {
                shared_game_state->players[connected_players_count].pid = message.player_id;
                char buf[32];
                sprintf(buf, "Gracz dolaczyl: %d\n", connected_players_count);
                my_print(buf);
                
                // Odpowiedz
                Message response = {.mtype = message.player_id, .player_id = connected_players_count};
                msgsnd(message_queue_id, &response, msg_size, 0);
                
                connected_players_count++;
                if (connected_players_count == MAX_PLAYERS) {
                    shared_game_state->game_active = 1;
                    my_print("Start Gry!\n");
                }
            }
            else if (message.mtype == MSG_ACTION && shared_game_state->game_active) {
                handle_client_action(shared_game_state, &message);
            }
            sem_unlock(semaphore_id);
        }

        // 2. Wysyłanie stanu gry
        sem_lock(semaphore_id);
        if (connected_players_count == MAX_PLAYERS) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (shared_game_state->players[i].pid != 0) {
                    Message update = {.mtype = MSG_STATE + i, .state_copy = shared_game_state->players[i]};
                    msgsnd(message_queue_id, &update, msg_size, 0);
                }
            }
        }
        sem_unlock(semaphore_id);
        usleep(200000); // 200ms
    }
}

int main() {
    my_print("Serwer Start...\n");

    // Czyszczenie starych zasobów IPC przed startem
    msgctl(msgget(KEY_MSG, 0666), IPC_RMID, NULL);
    shmctl(shmget(KEY_SHM, 0, 0666), IPC_RMID, NULL);
    semctl(semget(KEY_SEM, 0, 0666), 0, IPC_RMID, NULL);

    int message_queue_id = msgget(KEY_MSG, IPC_CREAT | 0666);
    int shared_memory_id = shmget(KEY_SHM, sizeof(GameSharedMemory), IPC_CREAT | 0666);
    int semaphore_id = semget(KEY_SEM, 1, IPC_CREAT | 0666);
    
    // Inicjalizacja pamięci współdzielonej i semafora
    GameSharedMemory *shared_game_state = (GameSharedMemory *)shmat(shared_memory_id, NULL, 0);
    semctl(semaphore_id, 0, SETVAL, 1);
    
    // Inicjalizacja danych gry
    memset(shared_game_state, 0, sizeof(GameSharedMemory)); // Zerowanie pamięci
    shared_game_state->players[0].resources = START_RESOURCES;
    shared_game_state->players[1].resources = START_RESOURCES;

    // Rozwidlenie procesów
    if (fork() == 0) {
        run_game_logic_process(semaphore_id, shared_game_state);
    } else {
        run_communication_process(message_queue_id, semaphore_id, shared_game_state);
    }
    return 0;
}
