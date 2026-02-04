#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h> 
#include "common.h"

#define ANSI_CLEAR  "\033[2J" // Wyczyść ekran
#define ANSI_HOME   "\033[H" // Przenieś kursor do domu
#define ANSI_SAVE   "\0337" // Zapisz pozycję kursora
#define ANSI_RESTORE "\0338" // Przywróć pozycję kursora
#define ANSI_RED    "\033[1;31m" // Czerwony
#define ANSI_YELLOW "\033[1;33m" // Żółty
#define ANSI_RESET  "\033[0m" // Reset kolorów
#define ANSI_ERASE_LINE "\033[2K" // Wyczyść linię
#define ANSI_UP     "\033[1A" // Przesuń kursor w górę o 1 linię
#define ANSI_CL_NL  "\033[K\n" // Wyczyść do końca linii i nowa linia

// Makro do wyświetlania jednostek
#define PRINT_UNIT(buf, len, idx, name, stats, s) \
    len += sprintf(buf+len, " [%d] %-21s %d (%s)" ANSI_CL_NL, idx, name, s.units[idx], stats) 

void print_temp(const char *str) {
    write(1, str, strlen(str));
    sleep(2);
    write(1, ANSI_UP ANSI_ERASE_LINE, 8);
}

int str_to_int(char *str) {
    int res = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') res = res * 10 + (str[i] - '0');
    }
    return res;
}

// Proces odpowiedzialny za wyświetlanie stanu gry (odbiera MSG_STATE)
void run_display_process(int message_queue_id, int player_index, int *local_resources) {
    Message received_message;
    char display_buffer[4096];
    size_t message_size = sizeof(Message) - sizeof(long);
    
    while(1) {
        if (msgrcv(message_queue_id, &received_message, message_size, MSG_STATE + player_index, 0) != -1) {
            PlayerState player_state = received_message.state_copy;
            *local_resources = player_state.resources;

            int buffer_length = 0;
            int income = BASE_INCOME + (player_state.units[UNIT_WORKER] * WORKER_INCOME);
            
            buffer_length += sprintf(display_buffer+buffer_length, ANSI_SAVE ANSI_HOME "=== GRA STRATEGICZNA ===" ANSI_CL_NL);
            buffer_length += sprintf(display_buffer+buffer_length, "Surowce:   %d (+%d/s)" ANSI_CL_NL, player_state.resources, income);
            buffer_length += sprintf(display_buffer+buffer_length, "Punkty:    %d/%d" ANSI_CL_NL, player_state.points, POINTS_TO_WIN);
            
            if (player_state.incoming_attack.active)
                buffer_length += sprintf(display_buffer+buffer_length, ANSI_RED "!!! ALARM: ZOSTALES ZAATAKOWANY - %ds !!!" ANSI_RESET ANSI_CL_NL, player_state.incoming_attack.time_left);
            else
                buffer_length += sprintf(display_buffer+buffer_length, ANSI_CL_NL);
            
            buffer_length += sprintf(display_buffer+buffer_length, player_state.last_event[0] ? "KOMUNIKAT: %s" ANSI_CL_NL : ANSI_CL_NL, player_state.last_event);
            buffer_length += sprintf(display_buffer+buffer_length, "--------------------------" ANSI_CL_NL);

            if (player_state.game_over) {
                buffer_length += sprintf(display_buffer+buffer_length, "\n" ANSI_YELLOW "*** KONIEC GRY ***" ANSI_RESET ANSI_CL_NL);
                buffer_length += sprintf(display_buffer+buffer_length, player_state.shutdown_timer > 0 ? "Zamkniecie za: %d s" ANSI_CL_NL : "Zamykanie..." ANSI_CL_NL, player_state.shutdown_timer);
            }
            
            buffer_length += sprintf(display_buffer+buffer_length, "Jednostki:" ANSI_CL_NL);
            PRINT_UNIT(display_buffer, buffer_length, UNIT_LIGHT,   "Lekka piechota:",  "K:100 A:1.0 D:1.2", player_state);
            PRINT_UNIT(display_buffer, buffer_length, UNIT_HEAVY,   "Ciezka piechota:", "K:250 A:1.5 D:3.0", player_state);
            PRINT_UNIT(display_buffer, buffer_length, UNIT_CAVALRY, "Jazda:",           "K:550 A:3.5 D:1.2", player_state);
            PRINT_UNIT(display_buffer, buffer_length, UNIT_WORKER,  "Robotnicy:",       "K:150 Prod: +5/s", player_state);
            
            buffer_length += sprintf(display_buffer+buffer_length, "--------------------------" ANSI_CL_NL "KOMENDY:" ANSI_CL_NL);
            buffer_length += sprintf(display_buffer+buffer_length, "b <typ> <ilosc>" ANSI_CL_NL "a <lekka> <ciezka> <jazda>" ANSI_CL_NL "q - wyjscie" ANSI_CL_NL);
            buffer_length += sprintf(display_buffer+buffer_length, "--------------------------" ANSI_CL_NL "PRODUKCJA:" ANSI_CL_NL);
            
            for (int slot = 0; slot < QUEUE_SIZE; slot++) {
                ProductionTask *task = &player_state.production_queue[slot];
                if (task->active)
                    buffer_length += sprintf(display_buffer+buffer_length, " Slot %d: Typ %d (x%d) - %ds" ANSI_CL_NL, slot, task->unit_type, task->count, task->time_left);
                else
                    buffer_length += sprintf(display_buffer+buffer_length, ANSI_CL_NL);
            }
            buffer_length += sprintf(display_buffer+buffer_length, ANSI_RESTORE);
            write(1, display_buffer, buffer_length);

            if (player_state.game_over && player_state.shutdown_timer <= 0) break;
        } else break;
    }
    
    system("stty icanon echo");
    kill(getppid(), SIGTERM);
    shmdt(local_resources);
    exit(0);
}

// Proces wejścia - czyta klawisze i wysyła komendy do serwera
void run_input_process(int message_queue_id, int player_index, int *local_resources_ptr, int child_pid, int local_shared_memory_id) {
    char buffer[128];
    int buf_pos = 0;
    Message action_message = {.mtype = MSG_ACTION, .player_id = player_index};
    size_t msg_size = sizeof(Message) - sizeof(long);

    // Konfiguracja terminala: tryb niekanoniczny (czytanie znak po znaku) i bez echa
    system("stty -icanon -echo");
    my_print("\033[30;1H> ");
    
    while(1) {
        char input_char;
        if (read(0, &input_char, 1) > 0) {
            if (input_char == 'q') {
                system("stty icanon echo");
                kill(child_pid, SIGTERM);
                shmdt(local_resources_ptr);
                shmctl(local_shared_memory_id, IPC_RMID, NULL);
                exit(0);
            }
            if (input_char == '\n') {
                // Zatwierdzenie komendy
                buffer[buf_pos] = '\0';
                buf_pos = 0;
                write(1, "\r\033[K", 4);
                
                // Parsowanie i Walidacja
                char *command = strtok(buffer, " ");
                if (command != NULL) {
                    int valid = 0;
                    action_message.player_id = player_index; // Refresh just in case

                    if (strcmp(command, "b") == 0) {
                        // Komenda buduj: b <typ> <ilosc>
                        char *type_str = strtok(NULL, " ");
                        char *count_str = strtok(NULL, " ");
                        if (type_str && count_str) {
                            int type = str_to_int(type_str);
                            int count = str_to_int(count_str);
                            if (type >= 0 && type <= 3 && (*local_resources_ptr >= UNIT_COST[type] * count)) {
                                action_message.action_type = ACTION_BUILD;
                                action_message.data[0] = type; action_message.data[1] = count;
                                valid = 1;
                            } else print_temp("Blad (typ/koszt)\n");
                        }
                    } 
                    else if (strcmp(command, "a") == 0) {
                        // Komenda atakuj: a <l> <c> <j>
                        char *unit1_str = strtok(NULL, " "), *unit2_str = strtok(NULL, " "), *unit3_str = strtok(NULL, " ");
                        if (unit1_str && unit2_str && unit3_str) {
                            action_message.action_type = ACTION_ATTACK;
                            action_message.data[0] = str_to_int(unit1_str); 
                            action_message.data[1] = str_to_int(unit2_str); 
                            action_message.data[2] = str_to_int(unit3_str);
                            valid = 1;
                        } else print_temp("Uzycie: a <l> <h> <c>\n");
                    }

                    if (valid) msgsnd(message_queue_id, &action_message, msg_size, 0);
                }
                my_print("> ");
            }
            else if (input_char == 127 || input_char == 8) {
                // Obsługa Backspace
                if (buf_pos > 0) { buf_pos--; write(1, "\b \b", 3); }
            }
            else if (buf_pos < 127) {
                buffer[buf_pos++] = input_char; write(1, &input_char, 1);
            }
        }
    }
}

int main() {
    int message_queue_id = msgget(KEY_MSG, 0666);
    if (message_queue_id == -1) { my_print("Blad kolejki\n"); exit(1); }

    my_print("Logowanie...\n");
    int my_pid = getpid();
    Message login_msg = {.mtype = MSG_LOGIN, .player_id = my_pid};
    msgsnd(message_queue_id, &login_msg, sizeof(Message) - sizeof(long), 0);

    Message response_msg;
    msgrcv(message_queue_id, &response_msg, sizeof(Message) - sizeof(long), my_pid, 0);
    int player_index = response_msg.player_id;
    my_print("Zalogowano!\n");
    
    my_print(ANSI_CLEAR);

    int local_shared_memory_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int *local_resources_ptr = (int *)shmat(local_shared_memory_id, NULL, 0);
    *local_resources_ptr = 0;

    int child_pid = fork();
    if (child_pid == 0) run_display_process(message_queue_id, player_index, local_resources_ptr);
    else run_input_process(message_queue_id, player_index, local_resources_ptr, child_pid, local_shared_memory_id);

    return 0;
}
