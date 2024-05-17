/*
-> while handling multiple clients, we need to introduce ACK bits for the accept or rejection of the request

-> Concurrent File Reading: While multiple clients can read the same file simultaneously,
 only one client can execute write operations on a file at any given time.

-> error codes handling

-> Logging and Message Display: Implement a logging mechanism where the NM records every request or acknowledgment received from
 clients or Storage Servers.
 Additionally, the NM should display or print relevant messages indicating the status and outcome of each operation.
  This bookkeeping ensures traceability and aids in debugging and system monitoring.

->IP Address and Port Recording: The log should include relevant information such as IP addresses and ports used
 in each communication, enhancing the ability to trace and diagnose issues.




*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <semaphore.h>

#define MAX_PATHS 10
#define MAX_PATH_LENGTH 256
#define MAX_STORAGE_SERVERS 10
#define BUFFER_SIZE 4096

#define MAX_TOKENS 10
#define MAX_TOKEN_LENGTH 256

#define NAMING_SERVER_IP "127.0.0.1"
#define NAMING_SERVER_PORT 8080

// Structure to store information about Storage Servers
struct StorageServer
{
    char name[50];
    char ip[20];
    int nm_port;
    int client_port;
    int num_acc_paths;
    int sock_ptr;
    char accessible_paths[MAX_PATHS][MAX_PATH_LENGTH];
};

char feedback[256];
sem_t sem_ss;
// Dummy storage server
struct StorageServer storage_server;

struct StorageServer storage_servers[MAX_STORAGE_SERVERS];
int num_storage_servers = 0;
int starting_storing_servers;

// Function that tokenizes the input string with space without changing the input string
void tokenize(const char *str, char tokens[MAX_TOKENS][MAX_TOKEN_LENGTH])
{
    char tempStr[MAX_TOKEN_LENGTH];              // Temporary string to avoid modifying the original
    strncpy(tempStr, str, MAX_TOKEN_LENGTH - 1); // Copy the input to the temporary string
    tempStr[MAX_TOKEN_LENGTH - 1] = '\0';        // Ensure null termination

    int i = 0;
    char *token = strtok(tempStr, " "); // Tokenize the temporary string
    while (token != NULL && i < MAX_TOKENS)
    {
        strncpy(tokens[i], token, MAX_TOKEN_LENGTH - 1);
        tokens[i][MAX_TOKEN_LENGTH - 1] = '\0'; // Ensure null termination
        token = strtok(NULL, " ");              // Get the next token
        i++;
    }
}

// Function to send feedback to clients
void sendClientFeedback(int client_sock, const char *feedback)
{
    // Send feedback to the client
    send(client_sock, feedback, strlen(feedback), 0);
}
#define MAX_CHILDREN 100

typedef struct Folder
{
    char name[100];
    struct Folder *children[MAX_CHILDREN];
} Folder;

// Structure representing a directory (or the root)
typedef struct Directory
{
    char ip[MAX_TOKEN_LENGTH];
    int nm_port;
    int c_port;
    Folder *root;
} Directory;
Directory *myDirectory;

Folder *createFolder(const char *name)
{
    Folder *newFolder = (Folder *)malloc(sizeof(Folder));
    if (newFolder != NULL)
    {
        strncpy(newFolder->name, name, sizeof(newFolder->name) - 1);
        newFolder->name[sizeof(newFolder->name) - 1] = '\0'; // Null-terminate the string
        for (int i = 0; i < MAX_CHILDREN; i++)
        {
            newFolder->children[i] = NULL;
        }
    }
    return newFolder;
}

// Function to create a new directory
Directory *createDirectory(char *ip, int nm_port, int c_port)
{
    Directory *newDirectory = (Directory *)malloc(sizeof(Directory));

    strcpy(newDirectory->ip, ip);
    newDirectory->nm_port = nm_port;
    newDirectory->c_port = c_port;
    newDirectory->root = createFolder("root");

    return newDirectory;
}

// Function to insert a path into the directory
void insert(Directory *directory, const char *path)
{
    char path_copy[100];
    strcpy(path_copy, path);

    Folder *currentFolder = directory->root;
    char *folderName = strtok(path_copy, "/");

    while (folderName != NULL)
    {
        // Check if the folder already exists
        int i;
        for (i = 0; i < MAX_CHILDREN; i++)
        {
            if (currentFolder->children[i] != NULL &&
                strcmp(currentFolder->children[i]->name, folderName) == 0)
            {
                break;
            }
        }

        // If the folder doesn't exist, create a new one
        if (i == MAX_CHILDREN)
        {
            for (i = 0; i < MAX_CHILDREN; i++)
            {
                if (currentFolder->children[i] == NULL)
                {
                    currentFolder->children[i] = createFolder(folderName);
                    break;
                }
            }
        }

        currentFolder = currentFolder->children[i];
        folderName = strtok(NULL, "/");
    }
}

// Function to search for a path in the directory
Folder *search(Directory *directory, const char *path)
{
    char path_copy[100];
    strcpy(path_copy, path);

    Folder *currentFolder = directory->root;
    char *folderName = strtok(path_copy, "/");

    while (folderName != NULL)
    {
        int i;
        for (i = 0; i < MAX_CHILDREN; i++)
        {
            if (currentFolder->children[i] != NULL &&
                strcmp(currentFolder->children[i]->name, folderName) == 0)
            {
                break;
            }
        }

        if (i == MAX_CHILDREN || currentFolder->children[i] == NULL)
        {
            return NULL; // Folder not found
        }

        currentFolder = currentFolder->children[i];
        folderName = strtok(NULL, "/");
    }

    return currentFolder;
}

Directory *directory_array[MAX_STORAGE_SERVERS];

#define MAX_CACHE_SIZE 15

struct Node
{
    int nm_port;
    int c_port;
    char ip[16];
    char path[256];
    struct Node *next;
    struct Node *prev;
};

struct Deque
{
    struct Node *front;
    struct Node *rear;
    int size;
};

// Function to initialize an empty deque
void initialize(struct Deque *deque)
{
    deque->front = NULL;
    deque->rear = NULL;
    deque->size = 0;
}

// Function to check if the deque is empty
int isEmpty(struct Deque *deque)
{
    return deque->front == NULL;
}

// Function to add a node to the rear of the deque
void enqueue(struct Deque *deque, int nm_port, int c_port, const char *ip, const char *path)
{
    struct Node *newNode = (struct Node *)malloc(sizeof(struct Node));
    if (newNode == NULL)
    {
        printf("Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    newNode->nm_port = nm_port;
    newNode->c_port = c_port;
    strncpy(newNode->ip, ip, sizeof(newNode->ip) - 1);
    newNode->ip[sizeof(newNode->ip) - 1] = '\0';
    strncpy(newNode->path, path, sizeof(newNode->path) - 1);
    newNode->path[sizeof(newNode->path) - 1] = '\0';

    newNode->next = NULL;
    newNode->prev = deque->rear;

    if (isEmpty(deque))
    {
        deque->front = newNode;
        deque->rear = newNode;
    }
    else
    {
        deque->rear->next = newNode;
        deque->rear = newNode;
    }

    if (deque->size == MAX_CACHE_SIZE)
    {
        // If the cache is full, remove the front node
        struct Node *temp = deque->front;
        deque->front = temp->next;
        if (deque->front != NULL)
        {
            deque->front->prev = NULL;
        }
        free(temp);
    }
    else
    {
        deque->size++;
    }
}

// Function to remove a node from the front of the deque
struct Node dequeue(struct Deque *deque)
{
    if (isEmpty(deque))
    {
        printf("Deque underflow\n");
        exit(EXIT_FAILURE);
    }

    struct Node temp = *deque->front;
    struct Node *tempNode = deque->front;

    deque->front = tempNode->next;
    if (deque->front != NULL)
    {
        deque->front->prev = NULL;
    }

    free(tempNode);

    deque->size--;

    return temp;
}

// Function to search for a node with a given path
struct Node searchByPath(struct Deque *deque, const char *searchPath)
{
    struct Node *current = deque->front;
    while (current != NULL)
    {
        if (strcmp(current->path, searchPath) == 0)
        {
            // Move the found node to the rear (as it's the most recently used)
            if (current != deque->rear)
            {
                if (current->prev != NULL)
                {
                    current->prev->next = current->next;
                }
                if (current->next != NULL)
                {
                    current->next->prev = current->prev;
                }
                current->prev = deque->rear;
                current->next = NULL;
                deque->rear->next = current;
                deque->rear = current;
            }

            return *current;
        }
        current = current->next;
    }

    // If not found, return a node with NULL values
    struct Node emptyNode;
    memset(&emptyNode, 0, sizeof(emptyNode));
    return emptyNode;
}

// Function to display the elements of the deque
void display(struct Deque *deque)
{
    struct Node *current = deque->front;
    while (current != NULL)
    {
        printf("nm_port: %d, c_port: %d, ip: %s, path: %s\n", current->nm_port, current->c_port, current->ip, current->path);
        current = current->next;
    }
    printf("\n");
}

struct Deque cache;

struct StorageServer findStorageServer(const char *path)
{
    /*
    -> trie implementation for finding the storage server along with LRU caching
    */
    // for (int i = 0; i < num_storage_servers; i++)
    // {
    //     for (int j = 0; j < storage_servers[i].num_acc_paths; j++)
    //     {
    //         // printf("%s", storage_servers[i].accessible_paths[j]);
    //         if (strcmp(path, storage_servers[i].accessible_paths[j]) == 0)
    //         {
    //             return storage_servers[i];
    //         }
    //     }
    // }
    struct StorageServer server_used;

    struct Node searchResult = searchByPath(&cache, path);
    if (searchResult.nm_port == 0)
    {
        printf("Not found in LRU cache\n");
        for (int i = 0; i < MAX_STORAGE_SERVERS; i++)
        {
            Directory *myDirectory = directory_array[i];
            Folder *resultFolder = search(myDirectory, path);

            if (resultFolder != NULL)
            {
                printf("Path found: c_port = %d, nm_port = %d, ip = %s\n",
                       myDirectory->c_port, myDirectory->nm_port, myDirectory->ip);
                strcpy(server_used.accessible_paths[0], path);
                server_used.client_port = myDirectory->c_port;
                strcpy(server_used.ip, myDirectory->ip);
                strcpy(server_used.name, "xxx");
                server_used.nm_port = myDirectory->nm_port;
                server_used.num_acc_paths = 1;

                return server_used; // actually struct. copy all teh things into the struct.
            }
            else
            {
                // printf("Path not found.\n");
                continue;
            }
        }
        printf("Path not found\n");
    }
    else
    {
        strcpy(server_used.accessible_paths[0],path);
        server_used.num_acc_paths=1;
        server_used.client_port=searchResult.c_port;
        strcpy(server_used.ip,searchResult.ip);
        strcpy(server_used.name,"xxx");
        server_used.nm_port=searchResult.nm_port;
        return server_used;
    }
}

char *sendCommandToStorageServer(const char *command, const char *ip, int port)
{

    // Create a socket for the client
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up the storage server address
    struct sockaddr_in storage_server_addr;
    storage_server_addr.sin_family = AF_INET;
    storage_server_addr.sin_addr.s_addr = inet_addr(ip);
    storage_server_addr.sin_port = htons(port);

    // Connect to the Storage Server
    if (connect(client_sock, (struct sockaddr *)&storage_server_addr, sizeof(storage_server_addr)) < 0)
    {
        perror("Connection to Storage Server failed");
        exit(EXIT_FAILURE);
    }

    // Send the command to the Storage Server
    send(client_sock, command, strlen(command), 0);

    // Receive feedback from the Storage Server
    memset(feedback, 0, sizeof(feedback));

    int receive = recv(client_sock, feedback, sizeof(feedback), 0);
    /*
    ->error code
    */
    // Display the feedback
    if (receive == -1)
    {
        perror("Receive failed");
    }
    else
    {
        printf("Feedback from Storage Server: %s\n", feedback);
    }

    // Close the connection
    close(client_sock);
    return feedback;
}

bool pathsBelongToSameServer(const char *path1, const char *path2)
{
    // Find the paths belonging to path1 and path2
    struct StorageServer ss1 = findStorageServer(path1);
    struct StorageServer ss2 = findStorageServer(path2);

    /*
    ->error code
    */
    // if (&ss1 == NULL || &ss2 == NULL)
    // {
    //     printf("Storage server does not exist.\n");
    //     return false;
    // }
    // Compare the ss1 and ss2 by everything, if they are equal, then return true
    if (strcmp(ss1.name, ss2.name) == 0 && strcmp(ss1.ip, ss2.ip) == 0 && ss1.nm_port == ss2.nm_port && ss1.client_port == ss2.client_port && ss1.num_acc_paths == ss2.num_acc_paths)
    {
        for (int i = 0; i < ss1.num_acc_paths; i++)
        {
            if (strcmp(ss1.accessible_paths[i], ss2.accessible_paths[i]) != 0)
            {
                return false;
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

int copyFile(const char *source, const char *destination)
{
    FILE *sourceFile, *destinationFile;
    char ch;

    sourceFile = fopen(source, "rb");
    if (sourceFile == NULL)
    {
        printf("Error: Unable to open the source file.\n");
        return -1;
    }

    destinationFile = fopen(destination, "wb");
    if (destinationFile == NULL)
    {
        fclose(sourceFile);
        printf("Error: Unable to create or open the destination file.\n");
        return -1;
    }

    while ((ch = fgetc(sourceFile)) != EOF)
    {
        fputc(ch, destinationFile);
    }

    fclose(sourceFile);
    fclose(destinationFile);
    printf("File copied successfully.\n");
    return 0;
}

int copyDirectory(const char *source, const char *destination)
{
    DIR *dir;
    struct dirent *entry;
    struct stat sourceStat;
    char sourcePath[256], destinationPath[256];

    if ((dir = opendir(source)) == NULL)
    {
        printf("Error: Unable to open source directory.\n");
        return -1;
    }

    if (stat(destination, &sourceStat) != 0)
    {
        // Create destination directory if it doesn't exist
        if (mkdir(destination, 0777) != 0)
        {
            printf("Error: Directory creation failed.\n");
            closedir(dir);
            return -1;
        }
    }

    while ((entry = readdir(dir)) != NULL)
    {
        sourcePath[1024] = '\0';
        destinationPath[1024] = '\0';

        strcat(sourcePath, source);
        strcat(sourcePath, "/");
        strcat(sourcePath, entry->d_name);

        strcat(destinationPath, destination);
        strcat(destinationPath, "/");
        strcat(destinationPath, entry->d_name);

        if (stat(sourcePath, &sourceStat) == 0)
        {
            if (S_ISDIR(sourceStat.st_mode))
            {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                {
                    // Recursively copy subdirectories and their contents
                    if (copyDirectory(sourcePath, destinationPath) != 0)
                    {
                        closedir(dir);
                        return -1;
                    }
                }
            }
            else
            {
                // Copy files to the destination directory
                if (copyFile(sourcePath, destinationPath) != 0)
                {
                    closedir(dir);
                    return -1;
                }
            }
        }
    }

    closedir(dir);
    printf("Directory copied successfully.\n");
    return 0;
}

// Function which accepts inputs from the clients
void *handdleLive(void *client_sock_ptr)
{
    int client_sock = *(int *)client_sock_ptr;
    char buffer[256];
    char tokens[10][256]; // Array of tokens
    int bytes_received;
    char FindPath[] = "FindPath";
    // Receive information from the Storage Server
    bytes_received = recv(client_sock, buffer, sizeof(buffer), 0);
    if (bytes_received > 0)
    {
        buffer[bytes_received] = '\0';

        // Process the received information (e.g., registration details from Storage Server)
        printf("Received from Someone: %s\n", buffer);

        // If a new storage server is to be added, receive the storage server struct and add it to the storage server array
        if (strcmp(buffer, "AddStorageServer") == 0)
        {
            recv(client_sock, &storage_server, sizeof(storage_server), 0);

            if (num_storage_servers < MAX_STORAGE_SERVERS)
            {
                sem_wait(&sem_ss);
                storage_servers[num_storage_servers] = storage_server;
                num_storage_servers++;
                sem_post(&sem_ss);
            }
            else
            {
                fprintf(stderr, "Maximum storage server capacity reached.\n");
            }
        }
        else if (strncmp(FindPath, buffer, strlen(FindPath)) == 0)
        {
            // Tokenize
            tokenize(buffer, tokens);
            // Extract the path
            char path[256];
            strcpy(path, tokens[1]);
            sem_wait(&sem_ss);
            struct StorageServer temp_ss = findStorageServer(path);
            sem_post(&sem_ss);
            if (temp_ss.name != NULL)
            {
                // Format the ip and port of the storage server and send it
                char ip_port[256];
                sprintf(ip_port, "%s:%d", temp_ss.ip, temp_ss.client_port);
                sendClientFeedback(client_sock, ip_port);
            }
            else
            {
                sendClientFeedback(client_sock, "No such path exists.");
            }
        }
        else if (strncmp("CreateFile", buffer, strlen("CreateFile")) == 0)
        {
            // Get the path out of the input. Input format: CreateFile <file_name> <path>
            tokenize(buffer, tokens);
            // Send it to the storage server and get the feedback and send the feedback to the client
            // Find the storage server which has the path
            sem_wait(&sem_ss);
            struct StorageServer temp_ss = findStorageServer(tokens[2]);
            sem_post(&sem_ss);
            if (temp_ss.name != NULL)
            {
                char feedback[1024];
                strcpy(feedback, sendCommandToStorageServer(buffer, temp_ss.ip, temp_ss.nm_port));
                printf("Sent to the SS %s\n", temp_ss.name);
                recv(temp_ss.sock_ptr, feedback, sizeof(feedback), 0);
                // printf("1\n");
                sendClientFeedback(client_sock, feedback);
            }
            else
            {
                sendClientFeedback(client_sock, "No such path exists.");
            }
        }
        else if (strncmp("DeleteFile", buffer, strlen("DeleteFile")) == 0)
        {
            // Get the path out of the input. Input format: CreateFile <file_name> <path>
            tokenize(buffer, tokens);
            // Send it to the storage server and get the feedback and send the feedback to the client
            // Find the storage server which has the path
            sem_wait(&sem_ss);
            struct StorageServer temp_ss = findStorageServer(tokens[2]);
            sem_post(&sem_ss);
            if (temp_ss.name != NULL)
            {
                char feedback[1024];
                strcpy(feedback, sendCommandToStorageServer(buffer, temp_ss.ip, temp_ss.nm_port));
                printf("Sent to the SS %s\n", temp_ss.name);
                recv(temp_ss.sock_ptr, feedback, sizeof(feedback), 0);
                // printf("1\n");
                sendClientFeedback(client_sock, feedback);
            }
            else
            {
                sendClientFeedback(client_sock, "No such path exists.");
            }
        }
        else if (strncmp("CopyFile", buffer, strlen("CopyFile")) == 0)
        {
            fflush(stdout);
            printf("insidelololol\n");

            char file_name[256], Path_1[256], Path_2[256];

            tokenize(buffer, tokens);
            strcpy(file_name, tokens[1]);
            strcpy(Path_1, tokens[2]);
            strcpy(Path_2, tokens[3]);
            if (tokens[0] != NULL)
            {
                // strncpy(file_name, token, sizeof(file_name));
                file_name[sizeof(file_name) - 1] = '\0'; // Ensure null-terminated string

                // Extract Path_1
                // token = strtok(NULL, " ");
                if (tokens[1] != NULL)
                {
                    // strncpy(Path_1, token, sizeof(Path_1));
                    Path_1[sizeof(Path_1) - 1] = '\0'; // Ensure null-terminated string

                    // Extract Path_2
                    // token = strtok(NULL, " ");
                    if (tokens[2] != NULL)
                    {
                        // strncpy(Path_2, token, sizeof(Path_2));
                        Path_2[sizeof(Path_2) - 1] = '\0'; // Ensure null-terminated string
                        // Assuming paths belong to different servers for demonstration purposes
                        // In a real scenario, you would need to compare paths and servers appropriately

                        // char startAck[] = "ACK: CopyFile operation starting";
                        // send(client_sock, startAck, strlen(startAck), 0);
                        sem_wait(&sem_ss);
                        int temp = pathsBelongToSameServer(Path_1, Path_2);
                        sem_post(&sem_ss);
                        // -> corresponding recv statement in client code must be written
                        if (temp)
                        {
                            // Relay it to the storage server and get the feedback
                            sem_wait(&sem_ss);
                            struct StorageServer temp_ss = findStorageServer(Path_1);
                            sem_post(&sem_ss);
                            if (temp_ss.name != NULL)
                            {
                                char feedback[1024];
                                strcpy(feedback, sendCommandToStorageServer(buffer, temp_ss.ip, temp_ss.nm_port));
                                printf("Sent to the SS %s\n", temp_ss.name);
                                recv(temp_ss.sock_ptr, feedback, sizeof(feedback), 0);
                                // printf("1\n");
                                sendClientFeedback(client_sock, feedback);
                            }
                            else
                            {
                                sendClientFeedback(client_sock, "No such path exists.");
                            }
                        }
                        else
                        {
                            // Make a new socket for calling one of the storage servers and send the command SendFile to the other storage server
                            // Get the storage server which has the path Path_1
                            sem_wait(&sem_ss);
                            struct StorageServer temp_ss = findStorageServer(Path_1);
                            struct StorageServer temp_ss2 = findStorageServer(Path_2);
                            sem_post(&sem_ss);
                            // Send ReceiveFile command to the temp_ss2
                            char sendCommand[1024];
                            char feedback[1024];
                            char feedback2[1024];

                            // sprintf(sendCommand,"PrepareCop",);
                            printf("Before first send\n");
                            sprintf(sendCommand, "SendFile %s %s %s %d", file_name, Path_1, temp_ss2.ip, temp_ss2.nm_port);

                            strcpy(feedback2, sendCommandToStorageServer(sendCommand, temp_ss.ip, temp_ss.nm_port));
                            printf("After first send\n");
                            memset(sendCommand, 0, sizeof(feedback));
                            sprintf(sendCommand, "ReceiveFile %s %s %s %d", file_name, Path_2, temp_ss.ip, temp_ss.nm_port);
                            printf("Before second send\n");

                            strcpy(feedback, sendCommandToStorageServer(sendCommand, temp_ss2.ip, temp_ss2.nm_port));
                            printf("After second send\n");

                            // Send SendFile command to the temp_ss

                            printf("COMMANDS SENT TO BOTH!!!!!!!\n");
                            // Send both of the feedbacks combined into one string to the client
                            strcat(feedback, feedback2);
                            sendClientFeedback(client_sock, feedback);
                        }
                    }
                    else
                    {
                        char errorResponse[] = "Error: Missing or invalid Path_2.";
                        send(client_sock, errorResponse, strlen(errorResponse), 0);
                    }
                }
                else
                {
                    char errorResponse[] = "Error: Missing or invalid Path_1.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
            }
            else
            {
                char errorResponse[] = "Error: Missing or invalid file_name.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }
        // ...
        else if (strncmp("CreateDirectory", buffer, strlen("CreateDirectory")) == 0)
        {
            // printf("Inside CreateDiredtory\n");
            // Relay this to the respective storage server
            // Get the path out of the input. Input format: CreateDirectory <directory_name> <path>
            tokenize(buffer, tokens);
            // Send it to the storage server and get the feedback and send the feedback to the client
            char feedback[256];
            // Find the storage server which has the path
            sem_wait(&sem_ss);
            struct StorageServer temp_ss = findStorageServer(tokens[2]);
            sem_post(&sem_ss);
            // Print all the accesible paths
            for (int i = 0; i < temp_ss.num_acc_paths; i++)
            {
                printf("%s\n", temp_ss.accessible_paths[i]);
            }

            if (temp_ss.name != NULL)
            {
                // Relay the request to the temp_ss using the storage server sock already created and get the feedback using the
                // send(temp_ss.sock_ptr, buffer, strlen(buffer), 0);
                // printf("BUFFER: %s\n", buffer);
                char feedback[1024];
                strcpy(feedback, sendCommandToStorageServer(buffer, temp_ss.ip, temp_ss.nm_port));
                printf("Sent to the SS %s\n", temp_ss.name);
                recv(temp_ss.sock_ptr, feedback, sizeof(feedback), 0);
                if (strncmp("Success", feedback, strlen("Success")) == 0)
                {
                    insert(myDirectory, tokens[1]);
                }
                // printf("1\n");
                sendClientFeedback(client_sock, feedback);
            }
            else
            {
                sendClientFeedback(client_sock, "No such path exists.");
            }
        }
        else if (strncmp("DeleteDirectory", buffer, strlen("DeleteDirectory")) == 0)
        {
            // Relay this to the respective storage server
            // Get the path out of the input. Input format: DeleteDirectory <path>
            tokenize(buffer, tokens);
            // Send it to the storage server and get the feedback and send the feedback to the client
            // Find the storage server which has the path
            sem_wait(&sem_ss);
            struct StorageServer temp_ss = findStorageServer(tokens[1]);
            sem_post(&sem_ss);
            if (temp_ss.name != NULL)
            {
                char feedback[1024];
                strcpy(feedback, sendCommandToStorageServer(buffer, temp_ss.ip, temp_ss.nm_port));
                printf("Sent to the SS %s\n", temp_ss.name);
                recv(temp_ss.sock_ptr, feedback, sizeof(feedback), 0);
                if (strncmp("Success", feedback, strlen("Success")) == 0)
                {
                    // Insert deletion function
                }
                // printf("1\n");
                sendClientFeedback(client_sock, feedback);
            }
            else
            {
                sendClientFeedback(client_sock, "No such path exists.");
            }
        }
        // else if (strncmp("CopyDirectory", buffer, strlen("CopyDirectory")) == 0)
        // {
        //     char sourceDirectory[256], destinationDirectory[256];
        //     // Extract the source and destination directory paths from the command
        //     char *token = strtok(NULL, " "); // Assuming source directory is the next part of the command
        //     if (token != NULL)
        //     {
        //         strncpy(sourceDirectory, token, sizeof(sourceDirectory));
        //         sourceDirectory[sizeof(sourceDirectory) - 1] = '\0'; // Ensure null-terminated string

        //         // Extract the destination directory path
        //         token = strtok(NULL, " ");
        //         if (token != NULL)
        //         {
        //             strncpy(destinationDirectory, token, sizeof(destinationDirectory));
        //             destinationDirectory[sizeof(destinationDirectory) - 1] = '\0'; // Ensure null-terminated string

        //             // Call a function to copy directories recursively
        //             if (copyDirectory(sourceDirectory, destinationDirectory) == 0)
        //             {
        //                 // Directory copied successfully
        //                 char successResponse[] = "Success: Directory copied.";
        //                 send(client_sock, successResponse, strlen(successResponse), 0);
        //             }
        //             else
        //             {
        //                 // Handle directory copy failure
        //                 char errorResponse[] = "Error: Directory copy failed.";
        //                 send(client_sock, errorResponse, strlen(errorResponse), 0);
        //             }
        //             // Send acknowledgment back to the client
        //             char completionAck[] = "ACK: CopyDirectory operation completed";
        //             send(client_sock, completionAck, strlen(completionAck), 0);
        //             // -> Corresponding recv statement in client code.
        //         }
        //         else
        //         {
        //             // Handle missing or invalid destination directory path
        //             char errorResponse[] = "Error: Missing or invalid destination directory path.";
        //             send(client_sock, errorResponse, strlen(errorResponse), 0);
        //         }
        //     }
        //     else
        //     {
        //         // Handle missing or invalid source directory path
        //         char errorResponse[] = "Error: Missing or invalid source directory path.";
        //         send(client_sock, errorResponse, strlen(errorResponse), 0);
        //     }
        // }

        // For all other commands, forward it to the storage server and send the feedback received from it to the client back
        else // Change this, this is wrong!! ~Vikas
        {
            // Send the command to the Storage Server
            send(client_sock, buffer, strlen(buffer), 0);

            // Receive feedback from the Storage Server
            char feedback[256];
            memset(feedback, 0, sizeof(feedback));
            int receive = recv(client_sock, feedback, sizeof(feedback), 0);
            if (receive == -1)
            {
                perror("Receive failed.");
            }
            else
            {

                // Display the feedback
                printf("Feedback from Storage Server: %s\n", feedback);
            }

            // Send the feedback to the client
            sendClientFeedback(client_sock, feedback);
        }
    }
}

// Function to handle communication with Storage Servers
void *handleStorageServer(void *client_sock_ptr)
{
    int client_sock = *(int *)client_sock_ptr;

    char name[50];
    char ip[20];
    int nm_port;
    int client_port;
    char accessible_paths[MAX_PATHS][MAX_PATH_LENGTH];
    storage_server.num_acc_paths = 0;

    struct StorageServer storage_server;
    
    FILE *file = fopen("input_nserver.txt", "w");
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Receive data and write it to the file
    while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        // Null-terminate the received data
        buffer[bytes_received] = '\0';

        // Check for the STOP signal to end the reception
        if (strcmp(buffer, "STOP") == 0) {
            break;
        }

        // Write the received data to the file
        if (fputs(buffer, file) == EOF) {
            perror("Error writing to file");
            break;
        }
    }

    // Check for receive errorsReceived from Storage
    if (bytes_received == -1) {
        perror("recv error");
    }

    fclose(file);

    FILE *fp = fopen("input_nserver.txt", "r");

    if (fp == NULL)
    {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
    fscanf(fp, "%s %s %d %d", name, ip, &nm_port, &client_port);
    int i = 0;
    while (fscanf(fp, "%s", accessible_paths[storage_server.num_acc_paths]) != EOF)
    {
        storage_server.num_acc_paths++;
    }
    fclose(fp);

    // Store the values in the struct
    strcpy(storage_server.name, name);
    strcpy(storage_server.ip, ip);
    storage_server.nm_port = nm_port;
    storage_server.client_port = client_port;
    for (int i = 0; i < storage_server.num_acc_paths; i++)
        {
            strcpy(storage_server.accessible_paths[i], accessible_paths[i]);
            // printf("%s\n", storage_server.accessible_paths[i]);
        }
    printf("Received from Storage Server: %s %s %d %d \n", storage_server.name, storage_server.ip, storage_server.client_port, storage_server.sock_ptr);

    Directory *myDirectory = createDirectory(storage_server.ip, storage_server.nm_port, storage_server.client_port);

    directory_array[num_storage_servers] = myDirectory;
    fflush(stdout);
    for (int i = 0; i < storage_server.num_acc_paths; i++)
    {
        printf("%s\n", storage_server.accessible_paths[i]);
        insert(myDirectory, storage_server.accessible_paths[i]);
    }

    if (num_storage_servers < MAX_STORAGE_SERVERS)
    {
        storage_servers[num_storage_servers] = storage_server;
        storage_servers[num_storage_servers].sock_ptr = client_sock;

        num_storage_servers++;
    }
    else
    {
        fprintf(stderr, "Maximum storage server capacity reached.\n");
    }

    // close(client_sock);
    free(client_sock_ptr);
    return NULL;
}

int main()
{
    sem_init(&sem_ss, 0, 1);
    printf("Enter the number of initial storage servers:");
    scanf("%d", &starting_storing_servers);

    // Create a socket for the Naming Server
    int naming_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (naming_server_sock == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up naming server address
    struct sockaddr_in naming_server_addr;
    naming_server_addr.sin_family = AF_INET;
    naming_server_addr.sin_addr.s_addr = inet_addr(NAMING_SERVER_IP);
    naming_server_addr.sin_port = htons(NAMING_SERVER_PORT);

    // Bind the Naming Server to the specified port
    if (bind(naming_server_sock, (struct sockaddr *)&naming_server_addr, sizeof(naming_server_addr)) < 0)
    {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming requests
    if (listen(naming_server_sock, 5) < 0)
    {
        perror("Listening failed");
        exit(EXIT_FAILURE);
    }

    printf("Naming Server listening on port %d...\n", NAMING_SERVER_PORT);
    int i = 0;

    initialize(&cache);
    for (int i = 0; i < starting_storing_servers; ++i)
        directory_array[i] = (Directory *)malloc(sizeof(Directory));
    // Accept storage servers upto starting_storage_servers
    while (i < starting_storing_servers)
    {
        // Accept incoming client connections (from Storage Servers)
        int *client_sock_ptr = (int *)malloc(sizeof(int));
        *client_sock_ptr = accept(naming_server_sock, (struct sockaddr *)NULL, NULL);

        // Create a separate thread to handle communication with the Storage Server
        pthread_t thread;
        pthread_create(&thread, NULL, handleStorageServer, client_sock_ptr);

        i++;
    }

    // Accept inputs from clients now
    while (1)
    {
        // Accept incoming client connections (from Storage Servers)
        int *client_sock_ptr = (int *)malloc(sizeof(int));
        *client_sock_ptr = accept(naming_server_sock, (struct sockaddr *)NULL, NULL);

        // Create a separate thread to handle communication with the Storage Server
        pthread_t thread;
        pthread_create(&thread, NULL, handdleLive, client_sock_ptr);
        // detach the thread
        pthread_detach(thread);
    }

    close(naming_server_sock);
    return 0;
}