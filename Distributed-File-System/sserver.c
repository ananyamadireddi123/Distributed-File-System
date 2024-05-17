#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdbool.h>
#include <dirent.h>
#include <semaphore.h>

int port_no = 6000;

#define MAX_PATHS 10
#define MAX_PATH_LENGTH 1024
#define MAX_STORAGE_SERVERS 10
#define BUFFER_SIZE 4096

#define NAMING_SERVER_IP "127.0.0.1" // Replace with the actual Naming Server IP
#define NAMING_SERVER_PORT 8080      // Replace with the actual Naming Server Port

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

typedef struct FileStruct {
    char path[256];
    sem_t mutex;      // Semaphore for updating the readers count
    sem_t writeLock;  // Semaphore for writers
    int readersCount; // Number of readers currently reading the file
} FileStruct;

   FILE *fp;
    FileStruct files[1000]; // Adjust size as needed
    int fileCount = 0;


// Initialize FileStruct
void initializeFile(FileStruct *file) {
    sem_init(&file->mutex, 0, 1);
    sem_init(&file->writeLock, 0, 1);
    file->readersCount = 0;
}

// Reader tries to enter
void startRead(FileStruct *file) {
    sem_wait(&file->mutex);
    file->readersCount++;
    if (file->readersCount == 1) {
        sem_wait(&file->writeLock);
    }
    sem_post(&file->mutex);
}

// Reader leaves
void endRead(FileStruct *file) {
    sem_wait(&file->mutex);
    file->readersCount--;
    if (file->readersCount == 0) {
        sem_post(&file->writeLock);
    }
    sem_post(&file->mutex);
}

// Writer tries to enter
void startWrite(FileStruct *file) {
    sem_wait(&file->writeLock);
}

// Writer leaves
void endWrite(FileStruct *file) {
    sem_post(&file->writeLock);
}

// Function to initialize semaphores for a file
void initializeFileSemaphore(FileStruct *file) {
    if (sem_init(&(file->mutex), 0, 1) != 0) {
        perror("Failed to initialize mutex semaphore");
        exit(EXIT_FAILURE);
    }
    if (sem_init(&(file->writeLock), 0, 1) != 0) {
        perror("Failed to initialize writeLock semaphore");
        sem_destroy(&(file->mutex));  // Clean up the already initialized semaphore
        exit(EXIT_FAILURE);
    }
    file->readersCount = 0;
}

void scanAndInitialize(const char *basePath) {
    char path[MAX_PATH_LENGTH];
    struct dirent *dp;
    DIR *dir = opendir(basePath);

    if (!dir) return;

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            snprintf(path, sizeof(path), "%s/%s", basePath, dp->d_name);

            struct stat statbuf;
            if (stat(path, &statbuf) != 0) continue;

            if (S_ISREG(statbuf.st_mode)) {
                strcpy(files[fileCount].path, path);
                initializeFileSemaphore(&files[fileCount]);
                fileCount++;
            } else if (S_ISDIR(statbuf.st_mode)) {
                scanAndInitialize(path);
            }
        }
    }

    closedir(dir);
}

FileStruct* getFileStruct(const char* path) {
    for (int i = 0; i < fileCount; i++) {
        printf("File Path: %s\n", files[i].path);
        if (strcmp(files[i].path, path) == 0) {
            return &files[i];
        }
    }
    return NULL; // Return NULL if no matching FileStruct is found
}


struct StorageServer storage_server;

void tokenize(char *str, char tokens[10][256])
{
    int i = 0;
    char *token = strtok(str, " ");
    while (token != NULL)
    {
        strcpy(tokens[i], token);
        token = strtok(NULL, " ");
        i++;
    }
}

int connect_to_destServer(const struct StorageServer *destStorageServer)
{
    int destServerSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (destServerSocket == -1)
    {
        perror("Destination Server socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in destServerAddr;
    destServerAddr.sin_family = AF_INET;
    destServerAddr.sin_addr.s_addr = inet_addr(destStorageServer->ip);
    destServerAddr.sin_port = htons(destStorageServer->client_port);

    if (connect(destServerSocket, (struct sockaddr *)&destServerAddr, sizeof(destServerAddr)) < 0)
    {
        perror("Connection to Destination Server failed");
        exit(EXIT_FAILURE);
    }

    return destServerSocket;
}

void send_file_to_destServer(int destServerSocket, const char *source, const char *destPath)
{
    FILE *sourceFile = fopen(source, "rb");
    if (sourceFile == NULL)
    {
        perror("Unable to open the source file");
        exit(EXIT_FAILURE);
    }

    // Send destination path to the server
    send(destServerSocket, destPath, strlen(destPath), 0);

    char buffer[1024];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), sourceFile)) > 0)
    {
        send(destServerSocket, buffer, bytesRead, 0);
    }

    fclose(sourceFile);
    close(destServerSocket); // Close the socket after file transfer
}

void *handleClientRequest(void *client_socket)
{

    int client_sock = *((int *)(client_socket));
    char cwd[1024];
    char tokens[10][256];
    char command[256];

    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        // printf("Current working dir: %s\n", cwd);
    }
    else
    {
        perror("getcwd() error");
    }

    memset(command, 0, sizeof(command));

    recv(client_sock, command, sizeof(command), 0);

    // if (strncmp(command, "SendFile", strlen("SendFile")) != 0) {
    //     recv(client_sock, command, sizeof(command), 0);
    // }

    // Receive the command from the client

    // Implement logic to execute different commands

    if (strncmp(command, "ReadFile", strlen("ReadFile")) == 0)
    {

        // Tokenize the command
        tokenize(command, tokens);
        char filename[256];
        // Extract the filename and path from the command
        strcpy(filename, tokens[1]);
        char path[256];
        strcpy(path, tokens[2]);
        // Append the path and the filename
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s%s/%s", cwd, path, filename);
        printf("%s\n", full_path);
        if (tokens[1] != NULL)
        {
            full_path[sizeof(full_path) - 1] = '\0'; // Ensure null-terminated string

            if (strlen(filename) > 0)
            {
                FileStruct* file_struct =  getFileStruct(full_path);
                startRead(file_struct);
                FILE *file = fopen(full_path, "r");
                if (file == NULL)
                {
                    // Handle file not found or other errors
                    printf("Unable to open the file for reading\n");
                    printf("File does not exist\n");
                    // Send an error response back to the Storage Server
                    char errorResponse[] = "Error: File not found or unable to open for reading.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
                else
                {
                    // char startAck[] = "ACK: READ operation starting";
                    // send(client_sock, startAck, strlen(startAck), 0);
                    //-> correspondingly write a recv statement in client code.
                    char buffer[256];
                    size_t bytesRead;

                    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
                    {
                        // Send the content of the file back to the Storage Server
                        send(client_sock, buffer, bytesRead, 0);
                    }

                    fclose(file);

                    // Send a success response back to the Storage Server
                    char successResponse[] = "STOP: File read completed.";
                    send(client_sock, successResponse, strlen(successResponse), 0);
                    // char completionAck[] = "ACK: Write operation completed";
                    // send(client_sock, completionAck, strlen(completionAck), 0);
                    //-> correspondingly write a recv statement in client code.
                }
                endRead(file_struct);
            }
            else
            {
                // Handle invalid filename (e.g., empty)
                printf("Invalid filename\n");
                // Send an error response back to the Storage Server
                char errorResponse[] = "Error: Invalid filename.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }
        else
        {
            // Handle missing or invalid filename
            printf("Missing filename\n");
            // Send an error response back to the Storage Server
            char errorResponse[] = "Error: Missing or invalid filename.";
            send(client_sock, errorResponse, strlen(errorResponse), 0);
        }
    }

    else if (strncmp(command, "WriteFile", strlen("WriteFile")) == 0)
    {
        char filename[256];
        // Tokenize
        tokenize(command, tokens);
        // Extract the filename and path from the command
        strcpy(filename, tokens[1]);
        char path[256];
        strcpy(path, tokens[2]);
        // Full path
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s%s/%s", cwd, path, filename);
        printf("%s", full_path);
        if (tokens[0] != NULL)
        {
            // strncpy(filename, token, sizeof(filename));
            full_path[sizeof(full_path) - 1] = '\0'; // Ensure null-terminated string

            if (strlen(full_path) > 0)
            {
                FileStruct* file_struct =  getFileStruct(full_path);
                startWrite(file_struct);
                FILE *file = fopen(full_path, "a"); // Append mode
                if (file == NULL)
                {
                    // Handle file not found or other errors
                    printf("Unable to open the file for writing\n");
                    // Send an error response back to the Storage Server
                    char errorResponse[] = "Error: File not found or unable to open for writing.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
                else
                {
                    // char startAck[] = "ACK: Write operation starting";
                    // send(client_sock, startAck, strlen(startAck), 0);
                    //-> correspondingly write a recv statement in client code.
                    char buffer[1024];
                    size_t bytesReceived;

                    while ((bytesReceived = recv(client_sock, buffer, sizeof(buffer) - 1, 0)) > 0)
                    {
                        if (strncmp(buffer, "STOP", strlen("STOP")) == 0)
                        {
                            break;
                        }
                        fwrite(buffer, 1, bytesReceived, file);
                        // If STOP is received then stop reading
                    }

                    fclose(file);

                    // Send a success response back to the Storage Server
                    char successResponse[] = "Success: File write completed.";
                    send(client_sock, successResponse, strlen(successResponse), 0);
                    // char completionAck[] = "ACK: Write operation completed";
                    // send(client_sock, completionAck, strlen(completionAck), 0);
                }
                endWrite(file_struct);
            }
            else
            {
                // Handle invalid filename (e.g., empty)
                printf("Invalid filename\n");
                // Send an error response back to the Storage Server
                char errorResponse[] = "Error: Invalid filename.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }
        else
        {
            // Handle missing or invalid filename
            printf("Missing filename\n");
            // Send an error response back to the Storage Server
            char errorResponse[] = "Error: Missing or invalid filename.";
            send(client_sock, errorResponse, strlen(errorResponse), 0);
        }
    }

    else if (strncmp(command, "GetFileInfo", strlen("GetFileInfo")) == 0)
    {
        char filename[256];
        // Tokenize
        tokenize(command, tokens);
        // Extract the filename and filepath from the command
        strcpy(filename, tokens[1]);
        char path[256];
        strcpy(path, tokens[2]);
        // Append the filename to the path
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s%s/%s", cwd, path, filename);
        printf("%s\n", full_path);
        if (tokens[0] != NULL)
        {
            // strncpy(filename, token, sizeof(filename));
            full_path[sizeof(full_path) - 1] = '\0'; // Ensure null-terminated string

            if (strlen(filename) > 0)
            {
                struct stat file_stat;
                if (stat(filename, &file_stat) == -1)
                {
                    // Handle file not found or other errors
                    printf("Unable to retrieve file information\n");
                    // Send an error response back to the Storage Server
                    char errorResponse[] = "Error: File not found or unable to retrieve file information.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
                else
                {
                    // Retrieve size and permissions
                    // char startAck[] = "ACK: GetInfo operation starting";
                    // send(client_sock, startAck, strlen(startAck), 0);
                    //-> correspondingly write a recv statement in client code.

                    off_t file_size = file_stat.st_size;
                    mode_t file_permissions = file_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

                    // Send size and permissions as a response back to the Storage Server
                    char response[512];
                    snprintf(response, sizeof(response), "File Size: %lld bytes, Permissions: %o", (long long)file_size, (unsigned int)file_permissions);
                    send(client_sock, response, strlen(response), 0);
                    // char completionAck[] = "ACK: GetInfo operation completed";
                    // send(client_sock, completionAck, strlen(completionAck), 0);
                    //-> correspondingly write a recv statement in client code.
                }
            }
            else
            {
                // Handle invalid filename (e.g., empty)
                printf("Invalid filename\n");
                // Send an error response back to the Storage Server
                char errorResponse[] = "Error: Invalid filename.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }
        else
        {
            // Handle missing or invalid filename
            printf("Missing filename\n");
            // Send an error response back to the Storage Server
            char errorResponse[] = "Error: Missing or invalid filename.";
            send(client_sock, errorResponse, strlen(errorResponse), 0);
        }
    }

    else if (strncmp("CreateFile", command, strlen("CreateFile")) == 0)
    {
        char filename[256];
        // Extract the filename from the command
        tokenize(command, tokens);
        if (tokens[0] != NULL)
        {
            strncpy(filename, tokens[1], sizeof(filename));
            filename[sizeof(filename) - 1] = '\0'; // Ensure null-terminated string

            if (strlen(filename) > 0)
            {
                // char startAck[] = "ACK: Create operation starting";
                // send(client_sock, startAck, strlen(startAck), 0);
                // -> corresponding recv statement in client code must be written
                // Valid filename
                char full_path[2048];
                snprintf(full_path, sizeof(full_path), "%s%s/%s", cwd, tokens[2], filename);
                fflush(stdout);
                printf("%s\n", full_path);

                // error code present

                FILE *check = fopen(full_path, "r");
                if (check == NULL)
                {
                    // fclose(check);
                    FILE *newFile = fopen(full_path, "w");
                    if (newFile == NULL)
                    {
                        // Handle file creation failure
                        printf("Unable to create a new file\n");
                        // Send an error response back to the Storage Server
                        char errorResponse[] = "Error: Unable to create a new file.";
                        send(client_sock, errorResponse, strlen(errorResponse), 0);
                    }
                    else
                    {
                        fclose(newFile);
                        // Send a success response back to the Storage Server
                        char successResponse[] = "Success: File created.";
                        send(client_sock, successResponse, strlen(successResponse), 0);
                        // char completionAck[] = "ACK: Create operation completed";
                        // send(client_sock, completionAck, strlen(completionAck), 0);
                        // -> correspondingly write the recv statement in clinet code.
                    }
                }
                else
                {
                    fclose(check);
                    char failureResponse[] = "File with the same name already exists. Use a different file name.";
                    send(client_sock, failureResponse, strlen(failureResponse), 0);

                }
                }
                else
                {
                    // Handle invalid filename (e.g., empty)
                    printf("Invalid filename\n");
                    // Send an error response back to the Storage Server
                    char errorResponse[] = "Error: Invalid filename.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
            }
            else
            {
                // Handle missing or invalid filename
                printf("Missing filename\n");
                // Send an error response back to the Storage Server
                char errorResponse[] = "Error: Missing or invalid filename.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }

        else if (strncmp("DeleteFile", command, strlen("DeleteFile")) == 0)
        {
            char filename[256];
            // Tokenize
            tokenize(command, tokens);
            if (tokens[0] != NULL)
            {
                strncpy(filename, tokens[1], sizeof(filename));
                filename[sizeof(filename) - 1] = '\0'; // Ensure null-terminated string

                if (strlen(filename) > 0)
                {
                    // char startAck[] = "ACK: Delete operation starting";
                    // send(client_sock, startAck, strlen(startAck), 0);
                    // -> corresponding recv statement in client code must be written

                    // Append the file name to the path
                    char full_path[2048];
                    snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, tokens[2], filename);
                    if (remove(full_path) != 0)
                    {
                        // Handle file deletion failure
                        // You can send an error response back to the Storage Server
                        char errorResponse[] = "Error: File deletion failed.";
                        send(client_sock, errorResponse, strlen(errorResponse), 0);
                    }
                    else
                    {
                        // Send a success response back to the Storage Server
                        char successResponse[] = "Success: File deleted.";
                        send(client_sock, successResponse, strlen(successResponse), 0);
                    }
                    // char completionAck[] = "ACK: Delete operation completed";
                    // send(client_sock, completionAck, strlen(completionAck), 0);
                    // -> correspondingly write the recv statement in clinet code.
                }
                else
                {
                    // Handle invalid filename (e.g., empty)
                    // You can send an error response back to the Storage Server
                    char errorResponse[] = "Error: Invalid filename.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
            }
            else
            {
                // Handle missing or invalid filename
                // You can send an error response back to the Storage Server
                char errorResponse[] = "Error: Missing or invalid filename.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }

        // For CopyFile
        else if (strncmp("CopyFile", command, strlen("CopyFile")) == 0)
        {
            // Tokenize
            tokenize(command, tokens);
            // Extract the filename, path1 and path2
            char filename[256];
            char sourcePath[256];
            char destPath[256];
            strcpy(filename, tokens[1]);
            strcpy(sourcePath, tokens[2]);
            strcpy(destPath, tokens[3]);
            // Check if the filename and paths are valid
            if (filename != NULL && sourcePath != NULL && destPath != NULL)
            {
                FILE *sourceFile, *destinationFile;
                char ch;

                // Append the file name to the sourcePath
                char full_sourcePath[2048];
                snprintf(full_sourcePath, sizeof(full_sourcePath), "%s%s/%s", cwd, sourcePath, filename);

                sourceFile = fopen(full_sourcePath, "rb");
                if (sourceFile == NULL)
                {
                    printf("Error: Unable to open the source file.\n");
                    // Send the error response back to the Storage Server
                    char errorResponse[] = "Error: Unable to open the source file.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                    // Return a void pointer
                    return NULL;
                }

                // Append the file name to the destPath
                char full_destPath[2048];
                snprintf(full_destPath, sizeof(full_destPath), "%s%s/%s", cwd, destPath, filename);
                FILE *check=fopen(full_destPath, "r");
                if(check!=NULL)
                {
                    fclose(check);
                    //printf("Error: Unable to create or open the destination file.\n");
                    // Send the error response back to the Storage Server
                    char errorResponse[] = "File with the same name already exists.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);

                    return NULL;

                }
                fclose(check);
                destinationFile = fopen(full_destPath, "wb");

                if (destinationFile == NULL)
                {
                    fclose(sourceFile);
                    printf("Error: Unable to create or open the destination file.\n");
                    // Send the error response back to the Storage Server
                    char errorResponse[] = "Error: Unable to create or open the destination file.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);

                    return NULL;
                }

                while ((ch = fgetc(sourceFile)) != EOF)
                {
                    fputc(ch, destinationFile);
                }

                fclose(sourceFile);
                fclose(destinationFile);
                printf("File copied successfully within the same server.\n");
                // Send the success response back to the Storage Server
                char successResponse[] = "Success: File copied successfully within the same server.";
                send(client_sock, successResponse, strlen(successResponse), 0);
            }
            else
            {
                // Handle invalid filename (e.g., empty)
                // You can send an error response back to the Storage Server
                char errorResponse[] = "Error: Invalid filename or incorrect format.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }
        // Implement sending file to the client/naming server
        else if (strncmp("SendFile", command, strlen("SendFile")) == 0)
        {
            // Tokenize
            tokenize(command, tokens);
            // Extract the filename, path, ip and port
            char filename[256];
            char sourcePath[256];
            char destIP[256];
            int destPort;

            strcpy(filename, tokens[1]);
            strcpy(sourcePath, tokens[2]);
            strcpy(destIP, tokens[3]);
            destPort = atoi(tokens[4]);

            // Check if the filename and paths are valid
            if (filename != NULL && sourcePath != NULL && destIP != NULL && destPort != 0)
            {
                FILE *sourceFile;
                char ch;

                // Append the file name to the sourcePath
                char full_sourcePath[2048];
                snprintf(full_sourcePath, sizeof(full_sourcePath), "%s%s/%s", cwd, sourcePath, filename);
                printf("..%s..\n", full_sourcePath);
                sourceFile = fopen(full_sourcePath, "rb");
                if (sourceFile == NULL)
                {
                    printf("Error: Unable to open the source file.\n");
                    // Send the error response back to the Storage Server
                    char errorResponse[] = "Error: Unable to open the source file.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                    // Return a void pointer
                    return NULL;
                }

                int destServerSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (destServerSocket == -1)
                {
                    perror("Destination Server socket creation failed");
                    exit(EXIT_FAILURE);
                }

                struct sockaddr_in destServerAddr;
                destServerAddr.sin_family = AF_INET;
                destServerAddr.sin_addr.s_addr = inet_addr(destIP);
                destServerAddr.sin_port = htons(destPort);
                printf("Before connect\n");
                if (connect(destServerSocket, (struct sockaddr *)&destServerAddr, sizeof(destServerAddr)) < 0)
                {
                    perror("Connection to Destination Server failed");
                    exit(EXIT_FAILURE);
                }
                printf("After connect\n");
                char buffer[1024];
                size_t bytesRead;

                while ((bytesRead = fread(buffer, 1, sizeof(buffer), sourceFile)) > 0)
                {
                    fflush(stdout);
                    printf("%s", buffer);
                    send(destServerSocket, buffer, bytesRead, 0);
                }

                fclose(sourceFile);
                close(destServerSocket); // Close the socket after file transfer
                printf("File sent successfully to the destination server.\n");
                // Send the success response back to the Storage Server
                char successResponse[] = "Success: File sent successfully to the destination server.";
                send(client_sock, successResponse, strlen(successResponse), 0);
            }
            else
            {
                // Handle invalid filename (e.g., empty)
                // You can send an error response back to the Storage Server
                char errorResponse[] = "Error: Invalid filename or incorrect format.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }

        // Implement receiving file from the client/naming server
        else if (strncmp("ReceiveFile", command, strlen("ReceiveFile")) == 0)
        {
            // Tokenize
            tokenize(command, tokens);
            // Extract the filename, path, ip and port
            char filename[256];
            char destPath[256];
            char sourceIP[256];
            int sourcePort;

            strcpy(filename, tokens[1]);
            strcpy(destPath, tokens[2]);
            strcpy(sourceIP, tokens[3]);
            sourcePort = atoi(tokens[4]);

            // Check if the filename and paths are valid
            if (filename != NULL && destPath != NULL && sourceIP != NULL && sourcePort != 0)
            {

                FILE *destinationFile;
                char ch;

                // Append the file name to the destPath
                char full_destPath[2048];
                snprintf(full_destPath, sizeof(full_destPath), "%s%s/%s", cwd, destPath, filename);
                FILE *check=fopen(full_destPath, "r");
                if(check!=NULL)
                {
                    fclose(check);
                    //printf("Error: Unable to create or open the destination file.\n");
                    // Send the error response back to the Storage Server
                    char errorResponse[] = "File with the same name already exists.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);

                    return NULL;

                }
                fclose(check);
                destinationFile = fopen(full_destPath, "wb");
                if (destinationFile == NULL)
                {
                    printf("Error: Unable to create or open the destination file.\n");
                    // Send the error response back to the Storage Server
                    char errorResponse[] = "Error: Unable to create or open the destination file.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);

                    return NULL;
                }

                int sourceServerSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (sourceServerSocket == -1)
                {
                    perror("Source Server socket creation failed");
                    exit(EXIT_FAILURE);
                }

                struct sockaddr_in sourceServerAddr;
                sourceServerAddr.sin_family = AF_INET;
                sourceServerAddr.sin_addr.s_addr = inet_addr(sourceIP);
                sourceServerAddr.sin_port = htons(sourcePort);

                if (connect(sourceServerSocket, (struct sockaddr *)&sourceServerAddr, sizeof(sourceServerAddr)) < 0)
                {
                    perror("Connection to Destination Server failed");
                    exit(EXIT_FAILURE);
                }

                char buffer[1024];
                size_t bytesReceived;

                while ((bytesReceived = recv(sourceServerSocket, buffer, sizeof(buffer), 0)) > 0)
                {
                    fwrite(buffer, 1, bytesReceived, destinationFile);
                }

                fclose(destinationFile);
                close(sourceServerSocket); // Close the socket after file transfer
                printf("File received successfully from the source server.\n");
                // Send the success response back to the Storage Server
                char successResponse[] = "Success: File received successfully from the source server.";
                send(client_sock, successResponse, strlen(successResponse), 0);
            }
            else
            {
                // Handle invalid filename (e.g., empty)
                // You can send an error response back to the Storage Server
                char errorResponse[] = "Error: Invalid filename or incorrect format.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }
        else if (strncmp("CreateDirectory", command, strlen("CreateDirectory")) == 0)
        {
            // printf("Inside CreateDirectory\n");
            char directoryName[256];
            char path[512];
            // Tokenize
            tokenize(command, tokens);
            // Extract the directory name
            strcpy(directoryName, tokens[1]);
            // Extract the path of the directory
            strcpy(path, tokens[2]);
            // Append the path and the directoryname
            char full_path[2048];
            snprintf(full_path, sizeof(full_path), "%s/%s/%s", cwd, path, directoryName);
            // Check if the directory name is valid
            if (full_path != NULL)
            {
                // strncpy(directoryName, token, sizeof(directoryName));
                full_path[sizeof(full_path) - 1] = '\0'; // Ensure null-terminated string

                if (strlen(full_path) > 0)
                {

                    if (mkdir(full_path, 0777) == 0)
                    {
                        // Directory created successfully
                        char successResponse[] = "Success: Directory created.";
                        send(client_sock, successResponse, strlen(successResponse), 0);
                    }
                    else
                    {
                        // Handle directory creation failure
                        char errorResponse[] = "Error: Directory creation failed.";
                        send(client_sock, errorResponse, strlen(errorResponse), 0);
                    }
                    // printf("After mkdir\n");
                    // Send acknowledgment back to the client
                    char completionAck[] = "ACK: CreateDirectory operation completed";
                    send(client_sock, completionAck, strlen(completionAck), 0);
                    // -> Corresponding recv statement in client code.
                }
                else
                {
                    // Handle invalid directory name (e.g., empty)
                    char errorResponse[] = "Error: Invalid directory name.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
            }
            else
            {
                // Handle missing or invalid directory name
                char errorResponse[] = "Error: Missing or invalid directory name.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }

        else if (strncmp("DeleteDirectory", command, strlen("DeleteDirectory")) == 0)
        {
            char directoryName[256];
            char path[2048];
            // Tokenize
            tokenize(command, tokens);
            // Extract the path of the directory
            strcpy(path, tokens[1]);
            // Append the path and the directoryname
            char full_path[4096];
            snprintf(full_path, sizeof(full_path), "%s%s", cwd, path);

            if (tokens[1] != NULL)
            {
                full_path[sizeof(full_path) - 1] = '\0'; // Ensure null-terminated string

                if (strlen(full_path) > 0)
                {
                    // Valid directory name
                    if (rmdir(full_path) == 0)
                    {
                        // Directory deleted successfully
                        char successResponse[] = "Success: Directory deleted.";
                        send(client_sock, successResponse, strlen(successResponse), 0);
                    }
                    else
                    {
                        // Handle directory deletion failure
                        char errorResponse[] = "Error: Directory deletion failed.";
                        send(client_sock, errorResponse, strlen(errorResponse), 0);
                    }
                    // Send acknowledgment back to the client
                    char completionAck[] = "ACK: DeleteDirectory operation completed";
                    send(client_sock, completionAck, strlen(completionAck), 0);
                    // -> Corresponding recv statement in client code.
                }
                else
                {
                    // Handle invalid directory name (e.g., empty)
                    char errorResponse[] = "Error: Invalid directory name.";
                    send(client_sock, errorResponse, strlen(errorResponse), 0);
                }
            }
            else
            {
                // Handle missing or invalid directory name
                char errorResponse[] = "Error: Missing or invalid directory name.";
                send(client_sock, errorResponse, strlen(errorResponse), 0);
            }
        }

        else
        {
            // Handle unknown command or error
            printf("Not a valid operation\n");
        }

        // Close the client socket
        close(client_sock);
    }

    // Make ths server sock and then handle the input using handLive function
    void *makeServerSock()
    {
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock == -1)
        {
            perror("Server socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(storage_server.ip);
        server_addr.sin_port = htons(storage_server.nm_port);

        if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Server socket bind failed");
            exit(EXIT_FAILURE);
        }

        if (listen(server_sock, 5) < 0)
        {
            perror("Server socket listen failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);

        while (true)
        {
            int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
            if (client_sock < 0)
            {
                perror("Server socket accept failed");
                exit(EXIT_FAILURE);
            }

            // // If command starts with "SendFile"
            // if (strncmp("SendFile", command, "SendFile") == 0)
            // {
            //     // Tokenize
            //     tokenize(command, tokens);
            //     // Make a special thread for accepting the file
            //     pthread_t file_thread;
            //     pthread_create(&file_thread, NULL, handleClientRequest, (void *)&client_sock);
            // }

            // Create a new thread to handle the client request
            pthread_t client_thread;
            pthread_create(&client_thread, NULL, handleClientRequest, (void *)&client_sock);
        }
    }

    void *makeClientSock()
    {
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock == -1)
        {
            perror("Server socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = inet_addr(storage_server.ip);
        server_addr.sin_port = htons(storage_server.client_port);

        if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            perror("Server socket bind failed");
            exit(EXIT_FAILURE);
        }

        if (listen(server_sock, 5) < 0)
        {
            perror("Server socket listen failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);

        while (true)
        {
            int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
            if (client_sock < 0)
            {
                perror("Server socket accept failed");
                exit(EXIT_FAILURE);
            }

            // // If command starts with "SendFile"
            // if (strncmp("SendFile", command, "SendFile") == 0)
            // {
            //     // Tokenize
            //     tokenize(command, tokens);
            //     // Make a special thread for accepting the file
            //     pthread_t file_thread;
            //     pthread_create(&file_thread, NULL, handleClientRequest, (void *)&client_sock);
            // }

            // Create a new thread to handle the client request
            pthread_t client_thread;
            pthread_create(&client_thread, NULL, handleClientRequest, (void *)&client_sock);
        }
    }

    void initializeStorageServer(struct StorageServer storage_server)
    {
        // Initialize this Storage Server by sending its details to the Naming Server
        int naming_server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (naming_server_sock == -1)
        {
            perror("Naming Server socket creation failed");
            exit(EXIT_FAILURE);
        }

        struct sockaddr_in naming_server_addr;
        naming_server_addr.sin_family = AF_INET;
        naming_server_addr.sin_addr.s_addr = inet_addr(NAMING_SERVER_IP);
        naming_server_addr.sin_port = htons(NAMING_SERVER_PORT);

        // Connect to the Naming Server
        if (connect(naming_server_sock, (struct sockaddr *)&naming_server_addr, sizeof(naming_server_addr)) < 0)
        {
            perror("Connection to Naming Server failed");
            exit(EXIT_FAILURE);
        }

        // Send information about this Storage Server to the Naming Server
        fflush(stdout);

        FILE *file = fopen("input.txt", "r");
    char buffer[BUFFER_SIZE];

    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Read from the file and send its content
    while (fgets(buffer, BUFFER_SIZE, file) != NULL) {
        if (send(naming_server_sock, buffer, strlen(buffer), 0) == -1) {
            perror("send error");
            break;
        }
    }

    // Check for read errors
    if (ferror(file)) {
        perror("Error reading file");
    }
    sleep(1);
    // Send "STOP" to signal the end of transmission
    if (send(naming_server_sock, "STOP", 4, 0) == -1) {
        perror("send error");
    }

    fclose(file);

        // Close the connection to the Naming Server
        close(naming_server_sock);

        fflush(stdout);
        printf("Accepted\n");

        // Make a folder same as its current name and then change the root to that folder
        char folder_name[50];
        strcpy(folder_name, storage_server.name);
        mkdir(folder_name, 0777);
        chdir(folder_name);
        // Make the current directory as the root directory
        char root[50];

        // Create two different threads for handling the client and naming server
        pthread_t client_thread, naming_server_thread;
        pthread_create(&client_thread, NULL, makeClientSock, NULL);
        // Create the thread for makeServerSock
        pthread_create(&naming_server_thread, NULL, makeServerSock, NULL);
        // Wait for the threads to finish
        pthread_join(client_thread, NULL);
        pthread_join(naming_server_thread, NULL);
    }

    int main()
    {
        // if (argc != 5)
        // {
        //     fprintf(stderr, "Usage: %s <ServerName> <IPAddress> <Port> <AccessiblePaths>\n", argv[0]);
        //     exit(EXIT_FAILURE);
        // }

        // Read all the info about the storage server from a file called input.txt and store it in variables
        FILE *fp;

        char name[50];
        char ip[20];
        int nm_port;
        int client_port;
        char accessible_paths[MAX_PATHS][MAX_PATH_LENGTH];
        storage_server.num_acc_paths = 0;
        fp = fopen("input.txt", "r");
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


        char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        // printf("Current working dir: %s\n", cwd);
    }
    else {
        perror("getcwd() error");
    }
        // Store the values in the struct
        strcpy(storage_server.name, name);
        strcpy(storage_server.ip, ip);
        storage_server.nm_port = nm_port;
        storage_server.client_port = client_port;
        for (int i = 0; i < storage_server.num_acc_paths; i++)
        {
            strcpy(storage_server.accessible_paths[i], accessible_paths[i]);
            printf("%s\n", storage_server.accessible_paths[i]);
        }
        scanAndInitialize(cwd);
        initializeStorageServer(storage_server);

        return 0;
    }
