#include <unistd.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
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
void run_display_process(int msgid, int my_id, int *shared_resources) {
    Message state_msg;
    char out_buf[4096];
    
    while(1) {
        // Odbior wiadomości ze stanem gry przeznaczonej dla tego gracza
        if (msgrcv(msgid, &state_msg, sizeof(Message) - sizeof(long), MSG_STATE + my_id, 0) != -1) {
            *shared_resources = state_msg.state_copy.resources; // Aktualizacja zasobów dla procesu wejścia

            int len = 0;
            // Budowanie bufora wyjściowego (podwójne buforowanie dla uniknięcia migotania)
            len += sprintf(out_buf + len, ANSI_SAVE ANSI_HOME "=== GRA STRATEGICZNA ===" ANSI_CL_NL);
            int income = 50 + (state_msg.state_copy.units[UNIT_WORKER] * 5);
            len += sprintf(out_buf + len, "Surowce:   %d (+%d/s)" ANSI_CL_NL, state_msg.state_copy.resources, income);
            len += sprintf(out_buf + len, "Punkty:    %d/5" ANSI_CL_NL, state_msg.state_copy.points);
            
            if (state_msg.state_copy.incoming_attack.active) {
                len += sprintf(out_buf + len, ANSI_RED "!!! ALARM: ZOSTALES ZAATAKOWANY - %ds !!!" ANSI_RESET ANSI_CL_NL, state_msg.state_copy.incoming_attack.time_left);
            } else len += sprintf(out_buf + len, ANSI_CL_NL);
            
            if (strlen(state_msg.state_copy.last_event) > 0) {
                 len += sprintf(out_buf + len, "KOMUNIKAT: %s" ANSI_CL_NL, state_msg.state_copy.last_event);
            } else len += sprintf(out_buf + len, ANSI_CL_NL);

            len += sprintf(out_buf + len, "--------------------------" ANSI_CL_NL);

            if (state_msg.state_copy.game_over) {
                len += sprintf(out_buf + len, "\n" ANSI_YELLOW "*** KONIEC GRY ***" ANSI_RESET ANSI_CL_NL);
                if (state_msg.state_copy.shutdown_timer > 0) {
                    len += sprintf(out_buf + len, "Zamkniecie za: %d s" ANSI_CL_NL, state_msg.state_copy.shutdown_timer);
                } else len += sprintf(out_buf + len, "Zamykanie..." ANSI_CL_NL);
            }
            
            len += sprintf(out_buf + len, "Jednostki:" ANSI_CL_NL);
            len += sprintf(out_buf + len, " [0] Lekka piechota:      %d (K:100 A:1.0 D:1.2)" ANSI_CL_NL, state_msg.state_copy.units[UNIT_LIGHT]);
            len += sprintf(out_buf + len, " [1] Ciezka piechota:     %d (K:250 A:1.5 D:3.0)" ANSI_CL_NL, state_msg.state_copy.units[UNIT_HEAVY]);
            len += sprintf(out_buf + len, " [2] Jazda:               %d (K:550 A:3.5 D:1.2)" ANSI_CL_NL, state_msg.state_copy.units[UNIT_CAVALRY]);
            len += sprintf(out_buf + len, " [3] Robotnicy:           %d (K:150 Prod: +5/s)" ANSI_CL_NL, state_msg.state_copy.units[UNIT_WORKER]);
            len += sprintf(out_buf + len, "--------------------------" ANSI_CL_NL);
            len += sprintf(out_buf + len, "KOMENDY:" ANSI_CL_NL);
            len += sprintf(out_buf + len, "b <typ> <ilosc>" ANSI_CL_NL);
            len += sprintf(out_buf + len, "a <lekka> <ciezka> <jazda>" ANSI_CL_NL);
            len += sprintf(out_buf + len, "q - wyjscie" ANSI_CL_NL);
            len += sprintf(out_buf + len, "--------------------------" ANSI_CL_NL "PRODUKCJA:" ANSI_CL_NL);
            
            for(int i=0; i<QUEUE_SIZE; i++) {
                if(state_msg.state_copy.production_queue[i].active) {
                     len += sprintf(out_buf + len, " Slot %d: Typ %d (x%d) - %ds" ANSI_CL_NL, 
                                    i, state_msg.state_copy.production_queue[i].unit_type,
                                    state_msg.state_copy.production_queue[i].count,
                                    state_msg.state_copy.production_queue[i].time_left);
                } else len += sprintf(out_buf + len, ANSI_CL_NL);
            }
            len += sprintf(out_buf + len, ANSI_RESTORE);
            write(1, out_buf, len);

            if (state_msg.state_copy.game_over && state_msg.state_copy.shutdown_timer <= 0) {
                 break;
            }
        } else break;
    }
    
    system("stty icanon echo");
    kill(getppid(), SIGTERM);
    shmdt(shared_resources);
    exit(0);
}

// Proces wejścia - czyta klawisze i wysyła komendy do serwera
void run_input_process(int msgid, int my_id, int *shared_resources, int child_pid, int shm_res_id) {
    char buffer[128];
    int buf_pos = 0;
    
    // Konfiguracja terminala: tryb niekanoniczny (czytanie znak po znaku) i bez echa
    system("stty -icanon -echo");
    my_print("\033[30;1H> ");
    
    while(1) {
        char c;
        if (read(0, &c, 1) > 0) {
            if (c == 'q') {
                system("stty icanon echo");
                kill(child_pid, SIGTERM);
                shmdt(shared_resources);
                shmctl(shm_res_id, IPC_RMID, NULL);
                exit(0);
            }
            if (c == '\n') {
                // Zatwierdzenie komendy
                buffer[buf_pos] = '\0';
                buf_pos = 0;
                write(1, "\r\033[K", 4);
                
                // Parsowanie i Walidacja
                char *cmd = strtok(buffer, " ");
                if (cmd != NULL) {
                    Message action_msg = {.mtype = MSG_ACTION, .player_id = my_id};
                    int valid = 0;

                    if (strcmp(cmd, "b") == 0) {
                        // Komenda buduj: b <typ> <ilosc>
                        char *t = strtok(NULL, " ");
                        char *n = strtok(NULL, " ");
                        if (t && n) {
                            int type = my_atoi(t);
                            int count = my_atoi(n);
                            if (type >= 0 && type <= 3 && (*shared_resources >= UNIT_COST[type] * count)) {
                                action_msg.action_type = ACTION_BUILD;
                                action_msg.data[0] = type; action_msg.data[1] = count;
                                valid = 1;
                            } else print_temp("Blad (typ/koszt)\n");
                        }
                    } 
                    else if (strcmp(cmd, "a") == 0) {
                        // Komenda atakuj: a <l> <c> <j>
                        char *u1 = strtok(NULL, " "), *u2 = strtok(NULL, " "), *u3 = strtok(NULL, " ");
                        if (u1 && u2 && u3) {
                            action_msg.action_type = ACTION_ATTACK;
                            action_msg.data[0] = my_atoi(u1); action_msg.data[1] = my_atoi(u2); action_msg.data[2] = my_atoi(u3);
                            valid = 1;
                        } else print_temp("Uzycie: a <l> <h> <c>\n");
                    }

                    if (valid) msgsnd(msgid, &action_msg, sizeof(Message) - sizeof(long), 0);
                }
                my_print("> ");
            }
            else if (c == 127 || c == 8) {
                // Obsługa Backspace
                if (buf_pos > 0) { buf_pos--; write(1, "\b \b", 3); }
            }
            else if (buf_pos < 127) {
                buffer[buf_pos++] = c; write(1, &c, 1);
            }
        }
    }
}

int main() {
    int msgid = msgget(KEY_MSG, 0666);
    if (msgid == -1) { my_print("Blad kolejki\n"); exit(1); }

    my_print("Logowanie...\n");
    int my_pid = getpid();
    Message login_msg = {.mtype = MSG_LOGIN, .player_id = my_pid};
    msgsnd(msgid, &login_msg, sizeof(Message) - sizeof(long), 0);

    Message response_msg;
    msgrcv(msgid, &response_msg, sizeof(Message) - sizeof(long), my_pid, 0);
    int my_id = response_msg.player_id;
    my_print("Zalogowano!\n");
    
    my_print(ANSI_CLEAR);

    int shm_res_id = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int *shared_resources = (int *)shmat(shm_res_id, NULL, 0);
    *shared_resources = 0;

    int child_pid = fork();
    if (child_pid == 0) run_display_process(msgid, my_id, shared_resources);
    else run_input_process(msgid, my_id, shared_resources, child_pid, shm_res_id);

    return 0;
}
