#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT 8080
#define MAX_CLIENTS 5
#define FIELD_SIZE 100

int client_sockets[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure to contain comprehensive registration data
typedef struct {
    char username[FIELD_SIZE];
    char first_name[FIELD_SIZE];
    char last_name[FIELD_SIZE];
    char phone[FIELD_SIZE];
    char email[FIELD_SIZE];
    char password[FIELD_SIZE];
} UserProfile;

void strip_newline(char *str) {
    while (*str) {
        if (*str == '\n' || *str == '\r') {
            *str = '\0';
            break;
        }
        str++;
    }
}

// Validation function: Name must only contain letters
int is_valid_name(const char *name) {
    if (strlen(name) == 0) return 0;
    for (int i = 0; name[i] != '\0'; i++) {
        if (isdigit((unsigned char)name[i])) {
            return 0; // Contains a number, invalid!
        }
    }
    return 1;
}

// Validation function: Basic structural check for email formatting
int is_valid_email(const char *email) {
    const char *at_ptr = strchr(email, '@');
    if (!at_ptr) return 0; // Missing '@'
    
    const char *dot_ptr = strrchr(email, '.');
    if (!dot_ptr) return 0; // Missing '.'
    
    // Check if '@' comes before '.' and they are not adjacent
    if (at_ptr >= dot_ptr || (dot_ptr - at_ptr) <= 1) return 0;
    
    // Check basic length requirements
    if (at_ptr == email || *(dot_ptr + 1) == '\0') return 0;

    return 1;
}

// Check credentials during traditional login against users.txt
int check_login(const char *username, const char *password) {
    FILE *file = fopen("users.txt", "r");
    if (!file) return 0;

    char f_user[FIELD_SIZE], f_first[FIELD_SIZE], f_last[FIELD_SIZE];
    char f_phone[FIELD_SIZE], f_email[FIELD_SIZE], f_pass[FIELD_SIZE];

    // Read all fields matching the file format schema
    while (fscanf(file, "%99s %99s %99s %99s %99s %99s", 
                  f_user, f_first, f_last, f_phone, f_email, f_pass) == 6) {
        if (strcmp(username, f_user) == 0 && strcmp(password, f_pass) == 0) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}

// Check duplication and append complete user metrics to database
int register_user(UserProfile user) {
    FILE *file = fopen("users.txt", "r");
    if (file) {
        char f_user[FIELD_SIZE], f_first[FIELD_SIZE], f_last[FIELD_SIZE];
        char f_phone[FIELD_SIZE], f_email[FIELD_SIZE], f_pass[FIELD_SIZE];
        while (fscanf(file, "%99s %99s %99s %99s %99s %99s", 
                      f_user, f_first, f_last, f_phone, f_email, f_pass) == 6) {
            if (strcmp(user.username, f_user) == 0) {
                fclose(file);
                return 0; // Username already taken
            }
        }
        fclose(file);
    }

    file = fopen("users.txt", "a");
    if (!file) return 0;
    
    // Save structured data cleanly separated by spaces
    fprintf(file, "%s %s %s %s %s %s\n", 
            user.username, user.first_name, user.last_name, 
            user.phone, user.email, user.password);
    fclose(file);
    return 1;
}

void broadcast_message(char *message, int sender_fd) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != 0 && client_sockets[i] != sender_fd) {
            send(client_sockets[i], message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[1024];
    char active_username[FIELD_SIZE] = {0};
    char login_password[FIELD_SIZE] = {0};
    int authenticated = 0;
    int connection_alive = 1;

    char *clear_screen = "\033[H\033[2J";

    while (!authenticated && connection_alive) {
        char menu[] = "\n=== SECURE ACCOUNT SERVICES ===\n1. Login\n2. Register New Profile\nChoose an option: ";
        send(client_fd, menu, strlen(menu), 0);

        memset(buffer, 0, sizeof(buffer));
        if (recv(client_fd, buffer, sizeof(buffer) - 1, 0) <= 0) { connection_alive = 0; break; }
        strip_newline(buffer);

        if (strcmp(buffer, "1") == 0) {
            send(client_fd, "Enter Username: ", 16, 0);
            memset(active_username, 0, FIELD_SIZE);
            if (recv(client_fd, active_username, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
            strip_newline(active_username);

            send(client_fd, "Enter Password: ", 16, 0);
            memset(login_password, 0, FIELD_SIZE);
            if (recv(client_fd, login_password, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
            strip_newline(login_password);

            if (check_login(active_username, login_password)) {
                send(client_fd, clear_screen, strlen(clear_screen), 0);
                send(client_fd, "Authentication verified successfully.\n\n", 39, 0);
                authenticated = 1;
            } else {
                send(client_fd, "Invalid credentials profile setup. Try again.\n", 46, 0);
            }
        } 
        else if (strcmp(buffer, "2") == 0) {
            UserProfile new_user;
            memset(&new_user, 0, sizeof(UserProfile));
            char retype_password[FIELD_SIZE] = {0};

            // 1. Choose Username
            send(client_fd, "Select unique username: ", 24, 0);
            if (recv(client_fd, new_user.username, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
            strip_newline(new_user.username);

            // 2. Validate First Name
            int valid_input = 0;
            while (!valid_input) {
                send(client_fd, "Enter First Name (Letters only): ", 33, 0);
                memset(new_user.first_name, 0, FIELD_SIZE);
                if (recv(client_fd, new_user.first_name, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
                strip_newline(new_user.first_name);

                if (is_valid_name(new_user.first_name)) {
                    valid_input = 1;
                } else {
                    send(client_fd, "-> Error: Name field cannot contain digits.\n", 44, 0);
                }
            }
            if (!connection_alive) break;

            // 3. Validate Last Name
            valid_input = 0;
            while (!valid_input) {
                send(client_fd, "Enter Last Name (Letters only): ", 32, 0);
                memset(new_user.last_name, 0, FIELD_SIZE);
                if (recv(client_fd, new_user.last_name, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
                strip_newline(new_user.last_name);

                if (is_valid_name(new_user.last_name)) {
                    valid_input = 1;
                } else {
                    send(client_fd, "-> Error: Name field cannot contain digits.\n", 44, 0);
                }
            }
            if (!connection_alive) break;

            // 4. Collect Phone Number
            send(client_fd, "Enter Phone Number: ", 20, 0);
            if (recv(client_fd, new_user.phone, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
            strip_newline(new_user.phone);

            // 5. Validate Email Structure
            valid_input = 0;
            while (!valid_input) {
                send(client_fd, "Enter Email Address: ", 21, 0);
                memset(new_user.email, 0, FIELD_SIZE);
                if (recv(client_fd, new_user.email, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
                strip_newline(new_user.email);

                if (is_valid_email(new_user.email)) {
                    valid_input = 1;
                } else {
                    send(client_fd, "-> Error: Invalid email string format (Requires missing '@' or '.').\n", 69, 0);
                }
            }
            if (!connection_alive) break;

            // 6. Validate Passwords Matching
            valid_input = 0;
            while (!valid_input) {
                send(client_fd, "Enter Account Password: ", 24, 0);
                memset(new_user.password, 0, FIELD_SIZE);
                if (recv(client_fd, new_user.password, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
                strip_newline(new_user.password);

                send(client_fd, "Confirm Retype Password: ", 25, 0);
                memset(retype_password, 0, FIELD_SIZE);
                if (recv(client_fd, retype_password, FIELD_SIZE - 1, 0) <= 0) { connection_alive = 0; break; }
                strip_newline(retype_password);

                if (strcmp(new_user.password, retype_password) == 0) {
                    valid_input = 1;
                } else {
                    send(client_fd, "-> Error: Password matrices do not match.\n", 42, 0);
                }
            }
            if (!connection_alive) break;

            // Save to File System and Auto-Login
            if (register_user(new_user)) {
                strncpy(active_username, new_user.username, FIELD_SIZE);
                send(client_fd, clear_screen, strlen(clear_screen), 0);
                send(client_fd, "Account registration finalized! Auto-logging you in...\n\n", 57, 0);
                authenticated = 1;
            } else {
                send(client_fd, "Username selected is already registered.\n", 41, 0);
            }
        } else {
            send(client_fd, "Selection not recognized.\n", 26, 0);
        }
    }

    // --- MAIN CHATROOM LAYER ---
    if (authenticated && connection_alive) {
        char welcome_banner[] = "=========================================\n       CONNECTED TO REALTIME ROOM        \n=========================================\nType chat data and press Enter.\n\n";
        send(client_fd, welcome_banner, strlen(welcome_banner), 0);

        char join_notification[250];
        snprintf(join_notification, sizeof(join_notification), "--> User %s has joined the room.\n", active_username);
        broadcast_message(join_notification, client_fd);

        while (1) {
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

            if (bytes_received <= 0) {
                char leave_notification[250];
                snprintf(leave_notification, sizeof(leave_notification), "<-- User %s left the chat connection.\n", active_username);
                broadcast_message(leave_notification, client_fd);
                break;
            }

            char format_buffer[1500];
            snprintf(format_buffer, sizeof(format_buffer), "[%s]: %s", active_username, buffer);
            printf("%s", format_buffer);
            broadcast_message(format_buffer, client_fd);
        }
    }

    close(client_fd);
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == client_fd) {
            client_sockets[i] = 0;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) return 1;

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(server_socket);
        return 1;
    }
    
    listen(server_socket, MAX_CLIENTS);
    printf("Profile Management Chat Server launched on port %d.\n", PORT);

    memset(client_sockets, 0, sizeof(client_sockets));

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int new_client = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);

        if (new_client < 0) continue;

        pthread_mutex_lock(&clients_mutex);
        int slot_found = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] == 0) {
                client_sockets[i] = new_client;
                slot_found = i;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (slot_found != -1) {
            int *client_ptr = malloc(sizeof(int));
            *client_ptr = new_client;
            pthread_t thread_id;
            pthread_create(&thread_id, NULL, handle_client, client_ptr);
            pthread_detach(thread_id);
        } else {
            close(new_client);
        }
    }

    close(server_socket);
    return 0;
}
