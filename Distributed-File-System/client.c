#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/*
Directory Mounting: [Old: Clients establish contact with the NM by pinging it with the mounted directory path.
 When a client requests a resource at a specific location within the NFS (e.g., “~/../mount/dir1/dir2…”),
the NM undertakes a comprehensive search across all registered Storage Servers (SSs),
encompassing the given directory structures - “dir1/dir1” - to locate the requested resource.]
This specification has been removed (you may implement it if you wish to).
Here’s how you do it now - say, the client passes READ dir1/dir2/file.txt to the NM -> the NM looks over
all the accessible paths in SS1, SS2, SS3… then sees that the path is present in SSx -> The NM gives relevant information about SSx to the client.
*/

#define NAMING_SERVER_IP "127.0.0.1"
#define NAMING_SERVER_PORT 8080

int main();

char feedback[256];

void sendCommandToNamingServer(const char *command)
{
    // Create a socket for the client
    int client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set up the naming server address
    struct sockaddr_in naming_server_addr;
    naming_server_addr.sin_family = AF_INET;
    naming_server_addr.sin_addr.s_addr = inet_addr(NAMING_SERVER_IP);
    naming_server_addr.sin_port = htons(NAMING_SERVER_PORT);

    // Connect to the Naming Server
    if (connect(client_sock, (struct sockaddr *)&naming_server_addr, sizeof(naming_server_addr)) < 0)
    {
        perror("Connection to Naming Server failed");
        close(client_sock);
        exit(EXIT_FAILURE);
    }

    printf("Sending to the NS: %s\n", command);

    // Send the command to the Naming Server in a loop to handle partial sends
    int total_sent = 0;
    int len = strlen(command);
    while (total_sent < len)
    {
        int sent = send(client_sock, command + total_sent, len - total_sent, 0);
        if (sent == -1)
        {
            perror("Send failed");
            close(client_sock);
            exit(EXIT_FAILURE);
        }
        total_sent += sent;
    }

    printf("Total bytes sent: %d\n", total_sent);
    printf("Waiting for feedback\n");

    // Receive feedback from the Naming Server
    char feedback[256];
    memset(feedback, 0, sizeof(feedback));
    int received = recv(client_sock, feedback, sizeof(feedback), 0);
    if (received == -1)
    {
        perror("Receive failed");
    }
    else
    {
        // Display the feedback
        printf("Feedback from Naming Server: %s\n", feedback);
    }

    // Close the connection
    close(client_sock);
}

void clearInputBuffer()
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF)
    {
    }
}

void sendCommandToStorageServer(const char *command, const char *ip, int port)
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

    // If command was ReadFile then receive the text from the Storage Server till it sends STOP
    if (strncmp(command, "ReadFile", strlen("ReadFile")) == 0)
    {
        // Receive the text from the Storage Server till it sends STOP
        char text[256];
        while (1)
        {
            memset(text, 0, sizeof(text));
            recv(client_sock, text, sizeof(text) - 1, 0);
            if (strncmp(text, "STOP", strlen("STOP")) == 0)
            {
                break;
            }
            printf("%s", text);
        }
    }
    else if (strncmp(command, "WriteFile", strlen("WriteFile")) == 0)
    {
        // If command was WriteFile then send the text to the Storage Server till the user enters STOP
        char text[256];
        while (strncmp(text, "STOP", strlen("STOP")) != 0)
        {
            memset(text, 0, sizeof(text));
            fgets(text, sizeof(text), stdin);
            text[strcspn(text, "\n")] = 0;
            // gets(text);
            // scanf("%s", text);
            printf("Before send\n");
            send(client_sock, text, strlen(text), 0);
            printf("After send\n");
        }
    }
    // else if (strncmp(command, "GetFileInfo", strlen("GetFileInfo")) == 0)
    // {

    // }
    // printf("\n");
    // Receive feedback from the Storage Server
    char feedback[256];
    memset(feedback, 0, sizeof(feedback));
    int receive=recv(client_sock, feedback, sizeof(feedback), 0);
    /*
    -> error code
    */
   if(receive==-1)
   {
     perror("Receive failed");
   }
   else
   {
    // Display the feedback
    printf("Feedback from Storage Server: %s\n", feedback);
   }

    // Close the connection
    close(client_sock);
}

void naming_server()
{
    // Flush the stdout
    fflush(stdout);
    printf("Enter the command you want to send to the Naming Server: \n");
    printf("Write BACK to go back.\n");
    char command[256];

    // Consume the /n from the previous input

    // scanf("%s", command);
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = 0;
    // scanf("%s", command);
    if (strcmp(command, "BACK") == 0)
    {
        main();
        exit(0);
    }
    sendCommandToNamingServer(command);
}

void storage_server()
{
    printf("Enter the command you want to send to the Storage Server: \n");
    printf("Write BACK to go back.\n");
    char command[256];
    // scanf("%s", command);
    fflush(stdin);
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = 0;
    // gets(command);

    // Input ip and port from the user
    char ip[256];
    int port;
    printf("Enter the IP address of the Storage Server: ");
    fflush(stdin);
    fgets(ip, sizeof(ip), stdin);
    ip[strcspn(ip, "\n")] = 0;
    // gets(ip);
    // scanf("%s", ip);
    printf("Enter the port of the Storage Server: ");
    scanf("%d", &port);

    if (strcmp(command, "BACK") == 0)
    {
        main();
        exit(0);
    }
    // Send the command to the storage server
    sendCommandToStorageServer(command, ip, port);
}

int main()
{
    int choice;
    // const char *commandPrefix = "CreateFile:";
    // char filename[256];

    // printf("Enter the relative path for the new file: ");
    // scanf("%255s", filename);

    // Concatenate the relative path to the command
    // char createFileCommand[512];
    // snprintf(createFileCommand, sizeof(createFileCommand), "%s%s", commandPrefix, filename);

    // Send the command to the Naming Server
    // sendCommandToNamingServer(createFileCommand);

    while (1)
    {
        // A small menu asking the user what they want to do
        printf("What do you want to do?\n");
        printf("1) Interact with the Naming Server.\n");
        printf("2) Interact with the Storage Server.\n");
        printf("3) Exit the program.\n");
        printf("Enter your choice: ");
        scanf("%d", &choice);
        fflush(stdout);
        clearInputBuffer();
        switch (choice)
        {
        case 1:
            naming_server();
            break;
        case 2:
            storage_server();
            break;
        case 3:
            exit(0);
            break;
        default:
            printf("Invalid choice.\n");
            break;
        }
    }

    return 0;
}
