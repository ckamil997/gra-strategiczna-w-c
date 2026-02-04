#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h> 
#include "common.h"

const int UNIT_COST[] = {100, 250, 550, 150};

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

void my_print(const char *str) { write(1, str, strlen(str)); }

int my_atoi(char *str) {
    int res = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] >= '0' && str[i] <= '9') res = res * 10 + (str[i] - '0');
    }
    return res;
}

void print_temp(const char *str) {
    write(1, str, strlen(str));
    sleep(2);
    write(1, ANSI_UP ANSI_ERASE_LINE, 8);
}

// Proces odpowiedzialny za wyświetlanie stanu gry (odbiera MSG_STATE)
void run_display_process(int message_queue_id, int player_index, int *local_resources_ptr) {
    Message state_message;
    char out_buf[4096];
    size_t msg_size = sizeof(Message) - sizeof(long);
    
    while(1) {
        // Odbior wiadomości ze stanem gry przeznaczonej dla tego gracza
        if (receive_message(message_queue_id, &state_message, msg_size, MSG_STATE + player_index, 0) != -1) {
            *local_resources_ptr = state_message.state_copy.resources; // Aktualizacja zasobów dla procesu wejścia

            int len = 0;
            // Budowanie bufora wyjściowego (podwójne buforowanie dla uniknięcia migotania)
            len += sprintf(out_buf + len, ANSI_SAVE ANSI_HOME "=== GRA STRATEGICZNA ===" ANSI_CL_NL);
            int income = 50 + (state_message.state_copy.units[UNIT_WORKER] * 5);
            len += sprintf(out_buf + len, "Surowce:   %d (+%d/s)" ANSI_CL_NL, state_message.state_copy.resources, income);
            len += sprintf(out_buf + len, "Punkty:    %d/5" ANSI_CL_NL, state_message.state_copy.points);
            
            if (state_message.state_copy.incoming_attack.active) {
                len += sprintf(out_buf + len, ANSI_RED "!!! ALARM: ZOSTALES ZAATAKOWANY - %ds !!!" ANSI_RESET ANSI_CL_NL, state_message.state_copy.incoming_attack.time_left);
            } else len += sprintf(out_buf + len, ANSI_CL_NL);
            
            if (strlen(state_message.state_copy.last_event) > 0) {
                 len += sprintf(out_buf + len, "KOMUNIKAT: %s" ANSI_CL_NL, state_message.state_copy.last_event);
            } else len += sprintf(out_buf + len, ANSI_CL_NL);

            len += sprintf(out_buf + len, "--------------------------" ANSI_CL_NL);

            if (state_message.state_copy.game_over) {
                len += sprintf(out_buf + len, "\n" ANSI_YELLOW "*** KONIEC GRY ***" ANSI_RESET ANSI_CL_NL);
                if (state_message.state_copy.shutdown_timer > 0) {
                    len += sprintf(out_buf + len, "Zamkniecie za: %d s" ANSI_CL_NL, state_message.state_copy.shutdown_timer);
                } else len += sprintf(out_buf + len, "Zamykanie..." ANSI_CL_NL);
            }
            
            len += sprintf(out_buf + len, "Jednostki:" ANSI_CL_NL);
            len += sprintf(out_buf + len, " [0] Lekka piechota:      %d (K:100 A:1.0 D:1.2)" ANSI_CL_NL, state_message.state_copy.units[UNIT_LIGHT]);
            len += sprintf(out_buf + len, " [1] Ciezka piechota:     %d (K:250 A:1.5 D:3.0)" ANSI_CL_NL, state_message.state_copy.units[UNIT_HEAVY]);
            len += sprintf(out_buf + len, " [2] Jazda:               %d (K:550 A:3.5 D:1.2)" ANSI_CL_NL, state_message.state_copy.units[UNIT_CAVALRY]);
            len += sprintf(out_buf + len, " [3] Robotnicy:           %d (K:150 Prod: +5/s)" ANSI_CL_NL, state_message.state_copy.units[UNIT_WORKER]);
            len += sprintf(out_buf + len, "--------------------------" ANSI_CL_NL);
            len += sprintf(out_buf + len, "KOMENDY:" ANSI_CL_NL);
            len += sprintf(out_buf + len, "b <typ> <ilosc>" ANSI_CL_NL);
            len += sprintf(out_buf + len, "a <lekka> <ciezka> <jazda>" ANSI_CL_NL);
            len += sprintf(out_buf + len, "q - wyjscie" ANSI_CL_NL);
            len += sprintf(out_buf + len, "--------------------------" ANSI_CL_NL "PRODUKCJA:" ANSI_CL_NL);
            
            for(int queue_index = 0; queue_index < QUEUE_SIZE; queue_index++) {
                if(state_message.state_copy.production_queue[queue_index].active) {
                     len += sprintf(out_buf + len, " Slot %d: Typ %d (x%d) - %ds" ANSI_CL_NL, 
                                    queue_index, state_message.state_copy.production_queue[queue_index].unit_type,
                                    state_message.state_copy.production_queue[queue_index].count,
                                    state_message.state_copy.production_queue[queue_index].time_left);
                } else len += sprintf(out_buf + len, ANSI_CL_NL);
            }
            len += sprintf(out_buf + len, ANSI_RESTORE);
            write(1, out_buf, len);

            if (state_message.state_copy.game_over && state_message.state_copy.shutdown_timer <= 0) {
                 break;
            }
        } else break;
    }
    
    system("stty icanon echo");
    kill(getppid(), SIGTERM);
    detach_shared_memory(local_resources_ptr);
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
                detach_shared_memory(local_resources_ptr);
                remove_shared_memory(local_shared_memory_id);
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
                            int type = my_atoi(type_str);
                            int count = my_atoi(count_str);
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
                            action_message.data[0] = my_atoi(unit1_str); action_message.data[1] = my_atoi(unit2_str); action_message.data[2] = my_atoi(unit3_str);
                            valid = 1;
                        } else print_temp("Uzycie: a <l> <h> <c>\n");
                    }

                    if (valid) send_message(message_queue_id, &action_message, msg_size, 0);
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
    int message_queue_id = get_message_queue_id(KEY_MSG, 0666);
    if (message_queue_id == -1) { my_print("Blad kolejki\n"); exit(1); }

    my_print("Logowanie...\n");
    int my_pid = getpid();
    Message login_msg = {.mtype = MSG_LOGIN, .player_id = my_pid};
    send_message(message_queue_id, &login_msg, sizeof(Message) - sizeof(long), 0);

    Message response_msg;
    receive_message(message_queue_id, &response_msg, sizeof(Message) - sizeof(long), my_pid, 0);
    int player_index = response_msg.player_id;
    my_print("Zalogowano!\n");
    
    my_print(ANSI_CLEAR);

    int local_shared_memory_id = get_shared_memory_id(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int *local_resources_ptr = (int *)attach_shared_memory(local_shared_memory_id, 0);
    *local_resources_ptr = 0;

    int child_pid = fork();
    if (child_pid == 0) run_display_process(message_queue_id, player_index, local_resources_ptr);
    else run_input_process(message_queue_id, player_index, local_resources_ptr, child_pid, local_shared_memory_id);

    return 0;
}
