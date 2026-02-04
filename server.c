#include <unistd.h>
#include <stdlib.h>
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

// --- LOGIKA GRY ---

// Funkcja pomocnicza do obliczania siły ataku/obrony
float calculate_strength(int *units, const float *multipliers, int count) {
    float sum = 0;
    for(int i=0; i<count; i++) sum += units[i] * multipliers[i];
    return sum;
}

// Funkcja odpowiedzialna za rozstrzyganie walki
void resolve_combat(GameSharedMemory *shared_game_state, int defender_id) {
    if (!shared_game_state->players[defender_id].incoming_attack.active) return;
    
    int attacker_id = shared_game_state->players[defender_id].incoming_attack.attacker_id;
    int *attacking_units = shared_game_state->players[defender_id].incoming_attack.units; 
    
    // Oblicz siły
    float attack_strength = calculate_strength(attacking_units, UNIT_ATTACK, 3);
    float defense_strength = calculate_strength(shared_game_state->players[defender_id].units, UNIT_DEFENSE, 4);
    if (defense_strength < 0.1) defense_strength = 0.1;

    float defender_attack_power = calculate_strength(shared_game_state->players[defender_id].units, UNIT_ATTACK, 4);

    // Rozstrzygniecie bitwy
    if (attack_strength > defense_strength) {
        // Atakujący wygrywa
        for (int i = 0; i < 4; i++) shared_game_state->players[defender_id].units[i] = 0;
        shared_game_state->players[attacker_id].points++;
        shared_game_state->players[defender_id].game_over = (shared_game_state->players[attacker_id].points >= 5);
        shared_game_state->players[attacker_id].game_over = (shared_game_state->players[attacker_id].points >= 5);
        
        sprintf(shared_game_state->players[attacker_id].last_event, "Bitwa WYGRANA! +1 Pkt");
        sprintf(shared_game_state->players[defender_id].last_event, "Bitwa PRZEGRANA! Straciles armie.");
    } else {
        // Obrońca się broni
        for (int i = 0; i < 4; i++) {
            int current = shared_game_state->players[defender_id].units[i];
            int loss = (int)(current * attack_strength / defense_strength);
            shared_game_state->players[defender_id].units[i] = (current > loss) ? current - loss : 0;
        }
        sprintf(shared_game_state->players[defender_id].last_event, "Obrona skuteczna! Poniesiono straty.");
        sprintf(shared_game_state->players[attacker_id].last_event, "Atak odparty.");
    }
    shared_game_state->players[attacker_id].last_event_timer = 5;
    shared_game_state->players[defender_id].last_event_timer = 5;

    // Straty atakujacego (kontratak obrońcy)
    float attacker_defense = calculate_strength(attacking_units, UNIT_DEFENSE, 3);
    if (attacker_defense < 0.1) attacker_defense = 0.1;

    float loss_ratio = (defender_attack_power > attacker_defense) ? 1.0 : (defender_attack_power / attacker_defense);
    
    for(int i=0; i<3; i++) {
        int loss = (int)(attacking_units[i] * loss_ratio);
        int surviving = attacking_units[i] - loss;
        if (surviving < 0) surviving = 0;
        shared_game_state->players[attacker_id].units[i] += surviving;
    }

    shared_game_state->players[defender_id].incoming_attack.active = 0;

    // Warunek konca gry
    if (shared_game_state->players[attacker_id].points >= 5) {
        shared_game_state->game_active = 0;
        shared_game_state->players[attacker_id].shutdown_timer = 5;
        shared_game_state->players[defender_id].shutdown_timer = 5;
        sprintf(shared_game_state->players[attacker_id].last_event, "GRATULACJE! Wygrales!");
        sprintf(shared_game_state->players[defender_id].last_event, "PRZEGRALES.");
    }
}

// Proces odpowiedzialny za całą logikę gry (surowce, produkcja, bitwy)
void run_game_logic_process(int semaphore_id, GameSharedMemory *shared_game_state) {
    while (1) {
        semaphore_lock(semaphore_id);
        
        // Obsługa zamykania gry (automatyczne wyłączenie po 5s)
        if (shared_game_state->players[0].game_over && !shared_game_state->game_active) {
            static int ticks = 0;
            if (++ticks >= 10) { // Co ok 1s
                ticks = 0;
                if (shared_game_state->players[0].shutdown_timer-- <= 0) {
                    semaphore_unlock(semaphore_id);
                    kill(getppid(), SIGTERM);
                    exit(0);
                }
                for(int i=1; i<MAX_PLAYERS; i++) shared_game_state->players[i].shutdown_timer = shared_game_state->players[0].shutdown_timer;
            }
            semaphore_unlock(semaphore_id);
            usleep(100000);
            continue;
        }
        
        if (!shared_game_state->game_active) { semaphore_unlock(semaphore_id); sleep(1); continue; }

        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (shared_game_state->players[i].pid == 0) continue;

            // Surowce: Worker * 5 + Baza(50)
            shared_game_state->players[i].resources += 50 + (shared_game_state->players[i].units[UNIT_WORKER] * 5);

            // Ataki
            if(shared_game_state->players[i].incoming_attack.active) {
                if(--shared_game_state->players[i].incoming_attack.time_left <= 0) resolve_combat(shared_game_state, i);
            }

            // Wiadomości - wygaszanie
            if(shared_game_state->players[i].last_event_timer > 0) {
                if(--shared_game_state->players[i].last_event_timer <= 0) shared_game_state->players[i].last_event[0] = '\0';
            }

            // Produkcja
            for (int queue_index = 0; queue_index < QUEUE_SIZE; queue_index++) {
                if (shared_game_state->players[i].production_queue[queue_index].active) {
                    if (--shared_game_state->players[i].production_queue[queue_index].time_left <= 0) {
                        int type = shared_game_state->players[i].production_queue[queue_index].unit_type;
                        shared_game_state->players[i].units[type]++;
                        shared_game_state->players[i].production_queue[queue_index].count--;
                        
                        if (shared_game_state->players[i].production_queue[queue_index].count > 0) {
                             shared_game_state->players[i].production_queue[queue_index].time_left = UNIT_TIME[type];
                        } else {
                            shared_game_state->players[i].production_queue[queue_index].active = 0;
                        }
                    }
                    break; // Kolejka FIFO - tylko pierwszy element
                }
            }
        }
        semaphore_unlock(semaphore_id);
        sleep(1);
    }
}

// --- KOMUNIKACJA ---

// Funkcja przetwarzająca akcje żądane przez klienta (Budowa/Atak)
void handle_client_action(GameSharedMemory *shared_game_state, Message *message) {
    int player_index = message->player_id;
    if (player_index < 0 || player_index >= MAX_PLAYERS || shared_game_state->players[player_index].pid == 0) return;

    if (message->action_type == ACTION_BUILD) {
        int type = message->data[0];
        int count = message->data[1];
        int cost = UNIT_COST[type] * count;

        if (type >= 0 && type <= 3 && count > 0 && shared_game_state->players[player_index].resources >= cost) {
            for(int queue_index = 0; queue_index < QUEUE_SIZE; queue_index++) {
                if(!shared_game_state->players[player_index].production_queue[queue_index].active) {
                    shared_game_state->players[player_index].production_queue[queue_index].active = 1;
                    shared_game_state->players[player_index].production_queue[queue_index].unit_type = type;
                    shared_game_state->players[player_index].production_queue[queue_index].count = count;
                    shared_game_state->players[player_index].production_queue[queue_index].time_left = UNIT_TIME[type];
                    shared_game_state->players[player_index].resources -= cost;
                    break;
                }
            }
        }
    } 
    else if (message->action_type == ACTION_ATTACK) {
        int attack_units[3] = {message->data[0], message->data[1], message->data[2]};
        int defender_id = (player_index == 0) ? 1 : 0;
        
        // Czy gracz ma tyle jednostek?
        if (shared_game_state->players[player_index].units[0] >= attack_units[0] && 
            shared_game_state->players[player_index].units[1] >= attack_units[1] &&
            shared_game_state->players[player_index].units[2] >= attack_units[2] &&
            (attack_units[0] + attack_units[1] + attack_units[2]) > 0 && 
            !shared_game_state->players[defender_id].incoming_attack.active) 
        {
            shared_game_state->players[defender_id].incoming_attack.active = 1;
            shared_game_state->players[defender_id].incoming_attack.attacker_id = player_index;
            shared_game_state->players[defender_id].incoming_attack.defender_id = defender_id;
            shared_game_state->players[defender_id].incoming_attack.time_left = 5;
            memcpy(shared_game_state->players[defender_id].incoming_attack.units, attack_units, sizeof(attack_units));
            
            shared_game_state->players[player_index].units[0] -= attack_units[0];
            shared_game_state->players[player_index].units[1] -= attack_units[1];
            shared_game_state->players[player_index].units[2] -= attack_units[2];

            sprintf(shared_game_state->players[player_index].last_event, "Atak wyslany (5s)");
            shared_game_state->players[player_index].last_event_timer = 5;
            sprintf(shared_game_state->players[defender_id].last_event, "ALARM! Nadchodzi atak!");
            shared_game_state->players[defender_id].last_event_timer = 5;
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
        if (receive_message(message_queue_id, &message, msg_size, -10, IPC_NOWAIT) != -1) {
            semaphore_lock(semaphore_id);
            
            if (message.mtype == MSG_LOGIN && connected_players_count < MAX_PLAYERS) {
                shared_game_state->players[connected_players_count].pid = message.player_id;
                my_print("Gracz dolaczyl: "); my_print_int(connected_players_count); my_print("\n");
                
                // Odpowiedz
                Message response = {.mtype = message.player_id, .player_id = connected_players_count};
                send_message(message_queue_id, &response, msg_size, 0);
                
                connected_players_count++;
                if (connected_players_count == MAX_PLAYERS) {
                    shared_game_state->game_active = 1;
                    my_print("Start Gry!\n");
                }
            }
            else if (message.mtype == MSG_ACTION && shared_game_state->game_active) {
                handle_client_action(shared_game_state, &message);
            }
            semaphore_unlock(semaphore_id);
        }

        // 2. Wysyłanie stanu gry
        semaphore_lock(semaphore_id);
        if (connected_players_count == MAX_PLAYERS) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (shared_game_state->players[i].pid != 0) {
                    Message update = {.mtype = MSG_STATE + i, .state_copy = shared_game_state->players[i]};
                    send_message(message_queue_id, &update, msg_size, 0);
                }
            }
        }
        semaphore_unlock(semaphore_id);
        usleep(200000); // 200ms
    }
}

int main() {
    my_print("Serwer Start...\n");

    // Czyszczenie starych zasobów IPC przed startem
    remove_message_queue(get_message_queue_id(KEY_MSG, 0666));
    remove_shared_memory(get_shared_memory_id(KEY_SHM, 0, 0666));
    remove_semaphore(get_semaphore_id(KEY_SEM, 0, 0666));

    int message_queue_id = get_message_queue_id(KEY_MSG, IPC_CREAT | 0666);
    int shared_memory_id = get_shared_memory_id(KEY_SHM, sizeof(GameSharedMemory), IPC_CREAT | 0666);
    int semaphore_id = get_semaphore_id(KEY_SEM, 1, IPC_CREAT | 0666);
    
    // Inicjalizacja pamięci współdzielonej i semafora
    GameSharedMemory *shared_game_state = (GameSharedMemory *)attach_shared_memory(shared_memory_id, 0);
    initialize_semaphore(semaphore_id, 0, 1);
    
    // Inicjalizacja danych gry
    memset(shared_game_state, 0, sizeof(GameSharedMemory));
    shared_game_state->players[0].resources = 300;
    shared_game_state->players[1].resources = 300;

    // Rozwidlenie procesów
    if (fork() == 0) {
        run_game_logic_process(semaphore_id, shared_game_state);
    } else {
        run_communication_process(message_queue_id, semaphore_id, shared_game_state);
    }
    return 0;
}
