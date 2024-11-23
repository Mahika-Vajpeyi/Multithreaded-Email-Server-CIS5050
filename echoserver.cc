#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h> 
#include <ctype.h>
#include <pthread.h>
#include <string>
#include <algorithm>
#include <iostream>
#include <vector>
#include <signal.h>

using namespace std; 

bool process_command(int client_fd, string& command);
void *worker(void *arg);
void handle_shutdown(int signum);
string trim(string& str);

//Vectors to store thread IDs and client socket file descriptors
vector<pthread_t> thread_ids;
vector<int> client_fds;

pthread_mutex_t vector_mutex = PTHREAD_MUTEX_INITIALIZER; 
int listen_fd;
bool verbose = false;

int main(int argc, char *argv[]) {
    //Signal for Ctrl+C
    signal(SIGINT, handle_shutdown);

	int p = 10000;
    int c;

	// Parse command-line options
	while ((c = getopt(argc, argv, "ap:v")) != -1) {
		switch (c) {
			case 'a':
				fprintf(stderr, "Mahika Vajpeyi SEAS login: mvajpeyi\n");
		        return 1;
			case 'p':
                p = atoi(optarg);
                printf("port number p is %d\n", p);
				break;
            case 'v':
                // Enable verbose mode
                verbose = true;
                break;
            case '?':
                // Handle missing argument or unknown option
                if (optopt == 'p')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                return 1;

            default:
                abort();
		}
	}

  //Sets up listening socket
  listen_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
       fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
       exit(1);
   }

  struct sockaddr_in servaddr; //Declares a structure to hold the server's address information, including IP address and port number
  bzero(&servaddr, sizeof(servaddr)); //Clears the memory allocated for servaddr by setting all bytes to zero

  servaddr.sin_family = AF_INET; //Specifies the address family as IPv4
  servaddr.sin_addr.s_addr = htons(INADDR_ANY);  //Sets the server's IP address
  servaddr.sin_port = htons(p); //Specifies the port number on which the server will listen for incoming connections

  // Associates the socket (listen_fd) with the specified address (servaddr) and port
  if(bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
      fprintf(stderr, "Cannot bind socket (%s)\n", strerror(errno));
      close(listen_fd);
      exit(1);
  }

  // Marks the socket as a passive socket that will be used to accept incoming connection requests
  // 100 is the backlog parameter that specifies the maximum number of pending connections that can be queued
  if(listen(listen_fd, 100) < 0){
      fprintf(stderr, "Cannot listen for incoming connections (%s)\n", strerror(errno));
      close(listen_fd);
      exit(1);
  }

  printf("Server is listening on port %d...\n", p);

  while(true) {
    struct sockaddr_in clientaddr; //Declares a structure to hold the client's address information upon connection
    socklen_t clientaddrlen = sizeof(clientaddr); //Sets the length of the Client Address Structure
    
    //Accepts an Incoming Connection; Stores the returned client socket file descriptor in the allocated memory pointed to by fd
    int fd_ptr = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
    if (fd_ptr < 0) {
      fprintf(stderr, "Cannot accept connection \n");
      exit(1);
    }

    if (verbose) {
        fprintf(stderr, "[%d] New connection\n", fd_ptr);  // Verbose: New connection
    }
    
    pthread_t thread;
    /*
    &thread: Pointer to the thread identifier
    NULL: Default thread attributes
    worker: The function that the thread will execute; responsible for handling client communication
    fd: Pointer to the client's socket file descriptor, which is passed as an argument to the worker function  
    */
    if(pthread_create(&thread, NULL, worker, (void *)&fd_ptr) != 0){
        cout << " error calling worker " << endl;
        fprintf(stderr, "Failed to create thread \n");
        close(fd_ptr);
    } else {
        pthread_mutex_lock(&vector_mutex);

        thread_ids.push_back(thread);
        client_fds.push_back(fd_ptr);

        pthread_mutex_unlock(&vector_mutex);
    }

    pthread_detach(thread); // Detach the thread so that resources are freed upon completion
    }
    close(listen_fd);
    return 0;
}

void *worker(void *arg) {
    int client_fd = *(int*)arg;

    //Sends greeting messsage
    const char* message = "+OK Server ready (Author: Mahika Vajpeyi / mvajpeyi)\r\n";
    int messageLength = strlen(message);

    if (write(client_fd, message, messageLength) < 0) { //Send bytes
        fprintf(stderr, "error sending greeting\n");
        exit(1);
    }

    if (verbose) {
        fprintf(stderr, "[%d] S: +OK Server ready\n", client_fd);  // Verbose: Server ready message
    }

    char read_buffer[2000];
    ssize_t bytes_read;

    // Clears the buffer before reading
    memset(read_buffer, 0, sizeof(read_buffer));
    string buffer;

    while (true) {
        //Reads data from the client socket
        bytes_read = read(client_fd, read_buffer, sizeof(read_buffer)-1);
        if (bytes_read < 0) {
            fprintf(stderr, "read failed");
            break;
        } else if (bytes_read == 0) {
            fprintf(stderr, "Client disconnected\n");
            break;
        }

        //Null terminates the buffer
        read_buffer[bytes_read] = '\0';

        // Appends the received data to the buffer
        buffer.append(read_buffer, bytes_read);

        //Processes all complete lines in the buffer
        size_t pos;
        while ((pos = buffer.find("\n")) != string::npos) {
            //Extracts the line (excluding the newline character)
            string line = buffer.substr(0, pos);

            //Removes carriage return if present (handles CRLF)
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (verbose) {
                fprintf(stderr, "[%d] C: %s\n", client_fd, line.c_str());  // Verbose: Command received
            }

            bool result = process_command(client_fd, line);
            //Removes the processed line from the buffer
            buffer.erase(0, pos + 1);

            if(!result){
                if (verbose) {
                    fprintf(stderr, "[%d] Closing connection\n", client_fd);  // Verbose: Connection closed
                }

                // Locks mutex before modifying the shared vectors
                pthread_mutex_lock(&vector_mutex);
                
                // Removes thread ID and client FD from the vectors
                auto it = find(client_fds.begin(), client_fds.end(), client_fd);
                if (it != client_fds.end()) {
                    int index = distance(client_fds.begin(), it);
                    thread_ids.erase(thread_ids.begin() + index);
                    client_fds.erase(it);
                }
                
                // Unlocks mutex after modification
                pthread_mutex_unlock(&vector_mutex);

                close(client_fd);
                pthread_exit(NULL);
                break;
            }
        }
    }
    if (verbose) {
        fprintf(stderr, "[%d] Connection closed\n", client_fd);  // Verbose: Connection closed
    }

    close(client_fd);
    pthread_exit(NULL);
}

bool process_command(int client_fd, string& command) {
    command = trim(command);  //Trims the command

    //Finds the position of the first space to split the command and its argument
    size_t space_pos = command.find(' ');

    //Extracts the command (before the space) and the argument (after the space)
    string cmd = (space_pos != string::npos) ? command.substr(0, space_pos) : command;
    string argument = (space_pos != string::npos) ? command.substr(space_pos + 1) : "";

    //Converts the command part to uppercase for case insensitivity
    transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "ECHO") {
        string message = "+OK " + command.substr(5) + "\r\n";

        //Sends the echoed message back to the client
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Error sending ECHO response\n");
        }

        if (verbose) {
            fprintf(stderr, "[%d] S: %s\n", client_fd, message.c_str());  // Verbose: Server response (ECHO)
        }

        return true;
    } else if (cmd == "QUIT" || cmd =="QUIT\r\n") {
        string message = "+OK Goodbye!\r\n";

        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "[%d] S: Error sending QUIT response\n", client_fd);
            return true;
        }

        if (verbose) {
            fprintf(stderr, "[%d] S: +OK Goodbye!\r\n", client_fd);  // Verbose: Server response (QUIT)
        }

        return false;
    } else {
        //Handles unknown commands
        string response = "-ERR Unknown command\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Error sending unknown command response\n");
        }

        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Unknown command\n", client_fd);  // Verbose: Unknown command response
        }
        return true;
    }
}

// Signal handler for SIGINT (Ctrl+C)
void handle_shutdown(int signum) {
    printf("\nReceived shutdown signal (Ctrl+C), shutting down server...\n");

    //Locks mutex before accessing shared vectors
    pthread_mutex_lock(&vector_mutex);
    
    //Iterates through the client socket file descriptors
    for (int client_fd : client_fds) {
        const char* shutdown_message = "-ERR Server shutting down\n";
        write(client_fd, shutdown_message, strlen(shutdown_message));  // Send shutdown message
        close(client_fd);  //Closes the client connection
    }

    //Closes the listening socket
    close(listen_fd);

    //Clears the vectors
    client_fds.clear();
    thread_ids.clear();

    //Unlocks mutex after modifications
    pthread_mutex_unlock(&vector_mutex);

    printf("Server shutdown complete.\n");
    exit(0);  // Terminates the program
}

// Function to trim leading and trailing whitespaces
string trim(string& str) {
    string result = str;

    // Removes leading whitespaces
    result.erase(result.begin(), find_if(result.begin(), result.end(), [](unsigned char ch) {
        return !isspace(ch);
    }));

    // Removes trailing whitespaces
    result.erase(find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
        return !isspace(ch);
    }).base(), result.end());

    return result;
}