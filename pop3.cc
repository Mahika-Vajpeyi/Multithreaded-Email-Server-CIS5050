#include <stdlib.h>
#include <stdio.h>
#include <openssl/md5.h>
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
#include <fstream>
#include <map>
#include <sys/file.h>
#include <sstream>
#include <iomanip>


using namespace std; 

// Enumeration for POP3 states
enum Pop3State {
    INIT,
    AUTH,
    USER,
    PASS,
    TRANSACTION,
    QUIT,
    UPDATE
};

bool process_command(int client_fd, string& command, bool& auth, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes, map<int, string>& message_indices, int mbox_fd);
void *worker(void *arg);
void handle_shutdown(int signum);
string trim(string& str);
void process_USER(string argument, int client_fd, string mail_dir, bool& auth, Pop3State& previousState, string& user);
void process_PASS(string argument, int client_fd, string mail_dir, bool& auth, Pop3State& previousState, string user,
                map<string, bool>& deletion_flags, map<string, int>& message_sizes, map<int, string>& message_indices, int mbox_fd);
void process_STAT(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, 
                map<string, bool>& deletion_flags, map<string, int>& message_sizes, map<int, string>& message_indices);
void process_LIST(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                map<string, int>& message_sizes, map<int, string>& message_indices);
void process_UIDL(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                map<string, int>& message_sizes, map<int, string>& message_indices);
void process_RETR(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                map<string, int>& message_sizes, map<int, string>& message_indices, int mbox_fd);
void process_DELE(string argument, int client_fd, Pop3State& previousState, map<string, bool>& deletion_flags, 
                map<int, string>& message_indices);
void process_RSET(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes);
void process_NOOP(string argument, int client_fd, Pop3State& previousState);
void process_QUIT(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags);

//Vectors to store thread IDs and client socket file descriptors
vector<pthread_t> thread_ids;
vector<int> client_fds;

// Global map for file mutexes
map<string, pthread_mutex_t> fileMutexMap;

pthread_mutex_t vector_mutex = PTHREAD_MUTEX_INITIALIZER; 
int listen_fd;
bool verbose = false;
string mail_dir;

void computeDigest(char *data, int dataLengthBytes, unsigned char *digestBuffer)
{
  /* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

  MD5_CTX c;
  MD5_Init(&c);
  MD5_Update(&c, data, dataLengthBytes);
  MD5_Final(digestBuffer, &c);
}

//converts hexadecimal representation of hash to string
string digestToHexString(const unsigned char* digest, size_t length) {
    ostringstream hexStream;
    hexStream << hex << setfill('0');

    for (size_t i = 0; i < length; ++i) {
        hexStream << setw(2) << static_cast<int>(digest[i]);
    }

    return hexStream.str();
}

int main(int argc, char *argv[]) {
    //Signal for Ctrl+C
    signal(SIGINT, handle_shutdown);

	int p = 11000;
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
	} if (optind < argc) {
        mail_dir = argv[optind];
    } else {
        //Checks if mail directory is provided
        fprintf(stderr, "Error: Mail directory argument is required.\n");
        return 1;
    }

  //Sets up listening socket
  listen_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
       fprintf(stderr, "Cannot open socket (%s)\n", strerror(errno));
       exit(1);
   }

    int opt = 1;
    int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &opt, sizeof(opt));
    if (ret < 0) {
        perror("Error in SOCKOPT");
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
        std::cout << " error calling worker " << endl;
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

    //Variables to store information about transaction
    bool auth = false;
    Pop3State previousState = INIT;
    string user = "";
    int mbox_fd;

    //Maps to be used for storing message information
    map<string, bool> deletion_flags;
    map<string, int> message_sizes;
    map<int, string> message_indices;

    //Sends greeting messsage
    const char* message = "+OK POP3 ready [localhost]\r\n";
    int messageLength = strlen(message);

    if (write(client_fd, message, messageLength) < 0) { //Send bytes
        fprintf(stderr, "error sending greeting\n");
        exit(1);
    }
    previousState = AUTH;

    if (verbose) {
        fprintf(stderr, "[%d] S: +OK POP3 ready [localhost]\n", client_fd);  // Verbose: Server ready message
    }

    char read_buffer[2000];
    ssize_t bytes_read;

    //Clears the buffer before reading
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

            bool result = process_command(client_fd, line, auth, previousState, user, deletion_flags, message_sizes, message_indices, mbox_fd);
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

bool process_command(int client_fd, string& command, bool& auth, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes, map<int, string>& message_indices, int mbox_fd) {
    command = trim(command);  //Trims the command

    //Finds the position of the first space to split the command and its argument
    size_t space_pos = command.find(' ');

    //Extracts the command (before the space) and the argument (after the space)
    string cmd = (space_pos != string::npos) ? command.substr(0, space_pos) : command;
    string argument = (space_pos != string::npos) ? command.substr(space_pos + 1) : "";

    //Converts the command part to uppercase for case insensitivity
    transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "USER") {
        process_USER(argument, client_fd, mail_dir, auth, previousState, user);
        return true;
    } else if (cmd == "PASS") {
        process_PASS(argument, client_fd, mail_dir, auth, previousState, user, deletion_flags, message_sizes, message_indices, mbox_fd);
        return true;
    } else if (cmd == "STAT") {
        process_STAT(argument, client_fd, mail_dir, previousState, user, deletion_flags, message_sizes, message_indices);
        return true;
    } else if (cmd == "LIST") {
        process_LIST(argument, client_fd, mail_dir, previousState, user, deletion_flags, message_sizes, message_indices);
        return true;
    } else if (cmd == "UIDL") {
        process_UIDL(argument, client_fd, mail_dir, previousState, user, deletion_flags, message_sizes, message_indices);
        return true;
    } else if (cmd == "RETR") {
        process_RETR(argument, client_fd, mail_dir, previousState, user, deletion_flags, message_sizes, message_indices, mbox_fd);
        return true;
    } else if (cmd == "DELE") {
        process_DELE(argument, client_fd, previousState, deletion_flags, message_indices);
        return true;
    } else if (cmd == "RSET") {
        process_RSET(argument, client_fd, mail_dir, previousState, user, deletion_flags, message_sizes);
        return true;
    } else if (cmd == "NOOP") {
        process_NOOP(argument, client_fd, previousState);
        return true;
    } else if (cmd == "QUIT" || cmd =="QUIT\r\n") {
        process_QUIT(argument, client_fd, mail_dir, previousState, user, deletion_flags);
        return false;
    } 
    else {
        //Handles unknown commands
        string response = "-ERR Not supported\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Error sending unknown command response\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Not supported\n", client_fd);  // Verbose: Unknown command response
        }
        return true;
    }
}

void process_USER(string argument, int client_fd, string mail_dir, bool& auth, Pop3State& previousState, string& user){
    //Checks for correct previous state
    if (previousState != AUTH && previousState != USER && previousState != PASS) {
        string response = "-ERR command not allowed\r\n";
        if(write(client_fd, response.c_str(), response.length()) < 0){
          fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Command not allowed\n", client_fd);  
        }
        return;
    }

    if (argument.empty()) {
        string response = "-ERR username missing\r\n";
        if(write(client_fd, response.c_str(), response.length()) < 0){
          fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Username missing\n", client_fd);  
        }
        return;
    }

    //Checks if another user is logged in
    if (auth) {
        string response = "-ERR another user logged in\r\n";
        if(write(client_fd, response.c_str(), response.length()) < 0){
          fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Another user logged in\n", client_fd);  
        }
        return;
    }

    //Removes @localhost from username
    size_t atPos = argument.find('@');
    argument = (atPos != string::npos) ? argument.substr(0, atPos) : argument;
    string mbox_file_path = mail_dir + "/" + argument + ".mbox";
    // cout<<mbox_file_path<<endl;

    //Checks if mbox file for user exists
    ifstream file(mbox_file_path);
    if (file.good()) {
        string response = "+OK user found\r\n";
        user = argument;
        
        if(write(client_fd, response.c_str(), response.length()) < 0){
          fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: +OK user found\n", client_fd);  
        }
    } else {
        string error_message = "-ERR no such user\r\n";
        cout<<"[S]: "<<error_message<<endl;
        if (write(client_fd, error_message.c_str(), error_message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR No such user\n", client_fd);  
        }
    }

    previousState = USER;
}

void process_PASS(string argument, int client_fd, string mail_dir, bool& auth, Pop3State& previousState, string user,
                map<string, bool>& deletion_flags, map<string, int>& message_sizes, map<int, string>& message_indices, int mbox_fd) {
    //Checks if user name has been provided
    if (previousState != USER) {
        string response = "-ERR enter username first\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR enter username first\n", client_fd);  
        }
        return;
    }

    //Checks if another user is logged in
    if (auth) {
        string response = "-ERR another user logged in\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Another user logged in\n", client_fd);  
        }
        return;
    }

    if (argument.empty()) {
        string response = "-ERR password missing\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR password missing\n", client_fd);  
        }
        return;
    }

    if (argument != "cis505") {
        string response = "-ERR incorrect password\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR incorrect password\n", client_fd);  
        }
        previousState = USER;
        return;
    }

    // Checks if user's mbox file can be opened
    string mbox_file_path = mail_dir + "/" + user + ".mbox";
    mbox_fd = open(mbox_file_path.c_str(), O_RDWR);
    if (mbox_fd < 0) {
        string response = "-ERR cannot open user's mbox file\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Cannot open user's mailbox\n", client_fd);  
        }
        return;
    }

    flock(mbox_fd, LOCK_EX | LOCK_NB);
    // Acquire the mutex for the recipient's mbox file
    if (fileMutexMap.find(user) == fileMutexMap.end()) {
        // Initialize the mutex if it does not exist
        pthread_mutex_t newMutex;
        pthread_mutex_init(&newMutex, nullptr);
        fileMutexMap[user] = newMutex;
    }
    pthread_mutex_t& fileMutex = fileMutexMap[user];

    // Lock the mutex
    pthread_mutex_lock(&fileMutex);

    // pthread_mutex_t& file_mutex = file_mutexes[user];
    // pthread_mutex_lock(&file_mutex);

    FILE* fp = fdopen(mbox_fd, "r");
    if (fp == nullptr) {
        string response = "-ERR cannot open user's mbox file\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR cannot open user's mailbox\n", client_fd);  
        }
        pthread_mutex_unlock(&fileMutex);
        flock(mbox_fd, LOCK_UN);
        close(mbox_fd); // Close the file descriptor if opening FILE* fails
        return;
    }

    // Now, process the mbox file to populate maps that store mailbox information 
    string line;
    string message;
    int message_index = 1;
    int currMsgSize = 0;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp)) {
        line = string(buffer);
        if (line.rfind("From ", 0) == 0) {
            if (!message.empty()) {
                unsigned char digest[MD5_DIGEST_LENGTH];
                computeDigest(const_cast<char*>(message.c_str()), message.size(), digest);
                string hash = digestToHexString(digest, MD5_DIGEST_LENGTH);
                deletion_flags[hash] = false;
                message_sizes[hash] = currMsgSize;
                message_indices[message_index++] = hash;
                message.clear();
                currMsgSize = 0;
            }
        } else {
            currMsgSize += line.length();
            message += line;
        }
    }

    // Handle the last message if present
    if (!message.empty()) {
        unsigned char digest[MD5_DIGEST_LENGTH];
        computeDigest(const_cast<char*>(message.c_str()), message.size(), digest);
        string hash = digestToHexString(digest, MD5_DIGEST_LENGTH);
        deletion_flags[hash] = false;
        message_sizes[hash] = currMsgSize;
        message_indices[message_index++] = hash;
    }

    //Cleanup
    fclose(fp);
    pthread_mutex_unlock(&fileMutex);
    flock(mbox_fd, LOCK_UN);

    //Confirm user can log in
    auth = true;
    string response = "+OK authenticated\r\n";
    if (write(client_fd, response.c_str(), response.length()) < 0) {
        fprintf(stderr, "Could not communicate with client\r\n");
    }
    if (verbose) {
            fprintf(stderr, "[%d] S: +OK authenticated\n", client_fd);  
        }
    previousState = TRANSACTION;

}

void process_STAT(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes, map<int, string>& message_indices){
    if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR Command not allowed\n", client_fd);  
        }
        return;
    }

    if (!argument.empty()) {
        string response = "-ERR no arguments allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR no arguments allowed\n", client_fd);  
        }
        return;
    }

    //Checks if user's mbox file can be opened
    string mbox_file_path = mail_dir + "/" + user + ".mbox";
    ifstream file(mbox_file_path);
    if (!file.good()) {
        string error_message = "-ERR No such user\r\n";
        if (write(client_fd, error_message.c_str(), error_message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR No such user\n", client_fd);  
        }
        return;
    }

    //Loops through mailbox information in maps to prepare response
    int num_msgs = 0;
    int total_msg_size = 0;

    for (const auto& [index, hash] : message_indices) {
        if (!deletion_flags[hash]) {
        num_msgs++;
        total_msg_size += message_sizes[hash];
        }
    }

    string response = "+OK " + to_string(num_msgs) + " " + to_string(total_msg_size) + "\r\n";
    cout<<"[S]: "<<response<<endl;
    if (write(client_fd, response.c_str(), response.length()) < 0) {
        fprintf(stderr, "Could not communicate with client\r\n");
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: +OK %d %d\r\n", client_fd, num_msgs, total_msg_size);
    }
}

void process_LIST(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes, map<int, string>& message_indices){
   if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR command not allowed\r\n", client_fd);
        }
        return;
    }

    //Checks if user wants information for a specific message
    if (argument.empty()) {
        //Checks if user's mbox file exists
        string buffer;
        string mbox_file_path = mail_dir + "/" + user + ".mbox";
        if (!ifstream(mbox_file_path).good()) {
            string error_message = "-ERR no such user\r\n";
            if (write(client_fd, error_message.c_str(), error_message.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR no such user\r\n", client_fd);
            }
            return;
        }

        //Loops through mailbox information in maps to prepare response
        int num_msgs = 0;
        int total_msg_size = 0;

        for (const auto& [index, hash] : message_indices) {
            if (!deletion_flags[hash]) {
                num_msgs++;
                int msg_size = message_sizes[hash];
                total_msg_size += msg_size;
                buffer += to_string(num_msgs) + " " + to_string(msg_size) + "\r\n";
            }
        }

        buffer = "+OK " + to_string(num_msgs) + " messages (" + to_string(total_msg_size) + " octets)\r\n" + buffer + ".\r\n";
        if (write(client_fd, buffer.c_str(), buffer.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: +OK %d messages (%d octets)\r\n%s.\r\n", client_fd, num_msgs, total_msg_size, buffer.c_str());
        }
    } else {
        //Checks if message index is valid
        int msg_index = stoi(argument);
        if (msg_index > static_cast<int>(message_indices.size()) || msg_index <= 0) {
            string response = "-ERR no such message\r\n";
            if (write(client_fd, response.c_str(), response.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR no such message\r\n", client_fd);
            }
            return;
        }

        //Checks if message has been deleted
        string hash = message_indices[msg_index];
        if (deletion_flags[hash]) {
            string response = "-ERR no such message\r\n";
            if (write(client_fd, response.c_str(), response.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR no such message\r\n", client_fd);
            }
            return;
        }

        int msg_size = message_sizes[hash];
        string response = "+OK " + to_string(msg_index) + " " + to_string(msg_size) + "\r\n";

        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: +OK %d %d\r\n", client_fd, msg_index, msg_size);
        }
    }
}

void process_UIDL(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes, map<int, string>& message_indices){
   if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR command not allowed\r\n", client_fd);
        }
        return;
    }

    //Checks if user wants information about a specific message
    if (argument.empty()) {
        string buffer;
        buffer += "+OK\r\n";
        string mbox_file_path = mail_dir + "/" + user + ".mbox";
        if (!ifstream(mbox_file_path).good()) {
            string error_message = "-ERR could not access mailbox\r\n";
            if (write(client_fd, error_message.c_str(), error_message.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR could not access mailbox\r\n", client_fd);
            }
            return;
        }

        //Checks for deleted messages
        for (const auto& [index, hash] : message_indices) {
            if (!deletion_flags[hash]) {
                buffer += to_string(index) + " " + hash + "\r\n";
            }
        }

        //Response to user
        buffer += ".\r\n";
        if (write(client_fd, buffer.c_str(), buffer.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            return;
        }
    } else {
        int msg_index = stoi(argument);
        //Checks if message is valid
        if (msg_index > static_cast<int>(message_indices.size()) || msg_index <= 0) {
            string response = "-ERR no such message\r\n";
            if (write(client_fd, response.c_str(), response.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR no such message\r\n", client_fd);
            }
            return;
        }

        //Checks if message has been deleted
        string hash = message_indices[msg_index];
        if (deletion_flags[hash]) {
            string response = "-ERR no such message\r\n";
            if (write(client_fd, response.c_str(), response.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR no such message\r\n", client_fd);
            }
            return;
        }

        //Sends response to user
        string response = "+OK " + to_string(msg_index) + " " + hash + "\r\n";
        // cout<<"[S]: "<<response<<endl;
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: +OK %d %s\r\n", client_fd, msg_index, hash.c_str());
        }
    }
}

void process_RETR(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes, map<int, string>& message_indices, int mbox_fd) {
    if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR command not allowed\r\n", client_fd);
        }
        return;
    }

    if (argument.empty()) {
        string response = "-ERR argument required\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR argument required\r\n", client_fd);
        }
        return;
    }

    //Checks if message index is valid
    int msg_index = stoi(argument);
    if (msg_index > static_cast<int>(message_indices.size()) || msg_index <= 0) {
        string response = "-ERR no such message\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR no such message\r\n", client_fd);
        }
        return;
    }

    string hash = message_indices[msg_index];
    //Checks if message has been deleted
    if (deletion_flags[hash]) {
        string response = "-ERR no such message\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR no such message\r\n", client_fd);
        }
        return;
    }

    string mbox_file_path = mail_dir + "/" + user + ".mbox";
    mbox_fd = open(mbox_file_path.c_str(), O_RDWR);

    //Applies a file-level lock (flock) to the mailbox file
    if (flock(mbox_fd, LOCK_EX | LOCK_NB) < 0) {
        string response = "-ERR cannot lock mailbox file\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR cannot lock mailbox file\r\n", client_fd);
        }
        return;
    }

    //Locks the mutex to prevent other threads from accessing the mailbox file concurrently
    pthread_mutex_lock(&fileMutexMap[user]);

    FILE* fp = fdopen(mbox_fd, "r");
    if (fp == nullptr) {
        string response = "-ERR cannot access mailbox\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR cannot access mailbox\r\n", client_fd);
        }
        pthread_mutex_unlock(&fileMutexMap[user]);
        // Unlock the file if fdopen fails
        flock(mbox_fd, LOCK_UN);  
        return;
    }

    //Reads the mailbox to find the message
    string line;
    string message;
    bool message_found = false;
    char buffer[1024];

    while (fgets(buffer, sizeof(buffer), fp)) {
        line = string(buffer);
        if (line.rfind("From ", 0) == 0) {
            if (message_found) {
                break; 
            }
            unsigned char digest[MD5_DIGEST_LENGTH];
            computeDigest(const_cast<char*>(message.c_str()), message.size(), digest);
            string hash_string = digestToHexString(digest, MD5_DIGEST_LENGTH);

            if (hash == hash_string) {
                message_found = true;
                break;
            } else {
                //Clear the current message buffer if it's not the one we need
                message.clear();
            }
        } else {
            message += line;
        }
    }

    //Handles the last message if it matches
    if (!message_found && !message.empty()) {
        unsigned char digest[MD5_DIGEST_LENGTH];
        computeDigest(const_cast<char*>(message.c_str()), message.size(), digest);
        string hash_string = digestToHexString(digest, MD5_DIGEST_LENGTH);
        
        if (hash == hash_string) {
            message_found = true;
        }
    }

    //Unlocks the mutex now that we're done reading the file
    pthread_mutex_unlock(&fileMutexMap[user]);

    //If the message was not found, return an error response
    if (!message_found) {
        string response = "-ERR message not found\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR message not found\r\n", client_fd);
        }
        flock(mbox_fd, LOCK_UN);  
        return;
    }

    //Sends the message to the client
    string response = "+OK " + to_string(message.size()) + " octets\r\n";
    if (write(client_fd, response.c_str(), response.length()) < 0) {
        fprintf(stderr, "Could not communicate with client\r\n");
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: +OK %zu octets\r\n", client_fd, message.size());
    }
    if (write(client_fd, message.c_str(), message.length()) < 0) {
        fprintf(stderr, "Could not communicate with client\r\n");
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: %s\r\n", client_fd, message.c_str());
    }
    string end_marker = ".\r\n";
    if (write(client_fd, end_marker.c_str(), end_marker.length()) < 0) {
        fprintf(stderr, "Could not communicate with client\r\n");
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: %s\r\n", client_fd, end_marker.c_str());
    }

    //Releases the file lock
    flock(mbox_fd, LOCK_UN);
}

void process_DELE(string argument, int client_fd, Pop3State& previousState, map<string, bool>& deletion_flags, map<int, string>& message_indices){
   if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR command not allowed\r\n", client_fd);
        }
        return;
    }

    if (argument.empty()) {
        string response = "-ERR argument missing\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR argument missing\n", client_fd);  
        }
        return;
    }

    //Checks for valid message index
    int msg_index = stoi(argument);
    if (msg_index > static_cast<int>(message_indices.size()) || msg_index <= 0) {
        string response = "-ERR no such message\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR No such message\n", client_fd);  
        }
        return;
    }

    //Checks if message has already been deleted
    string hash = message_indices[msg_index];
    if (deletion_flags[hash]) {
        string response = "-ERR message already deleted\r\n";
    
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR message already deleted\n", client_fd);  
        }
        return;
    }

    //Marks message as deleted and sends response to user
    deletion_flags[hash] = true;
    string response = "+OK " + argument + " deleted\r\n";
    if (write(client_fd, response.c_str(), response.length()) < 0) {
        fprintf(stderr, "Could not communicate with client\r\n");
    }
    if(verbose){
        fprintf(stderr, "[%d] S: +OK %s deleted\r\n", client_fd, argument.c_str());
    }
}

void process_RSET(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags, 
                  map<string, int>& message_sizes){
     if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR command not allowed\n", client_fd);  
        }
        return;
    }

    if (!argument.empty()) {
        string response = "-ERR RSET doesn't take any arguments\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR RSET doesn't take any arguments\n", client_fd);  
        }
        return;
    }

    //Resets all deletion flags
    for (auto& entry : deletion_flags) {
        entry.second = false;
    }

    string mbox_file_path = mail_dir + "/" + user + ".mbox";
    ifstream mbox_file(mbox_file_path);
    if (!mbox_file.is_open()) {
        string response = "-ERR unable to open mailbox\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR unable to open mailbox\n", client_fd);  
        }
        return;
    }

    //Recalculates total message size to prepare response to user
    int total_msg_size = 0;
    for (const auto& entry : message_sizes) {
        total_msg_size += entry.second;
    }

    int num_msgs = message_sizes.size();
    string response = "+OK mailbox has " + to_string(num_msgs) + " messages (" + to_string(total_msg_size) + " octets)\r\n";
    cout<<"[S]: "<<response<<endl;
    if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
    }

    if(verbose){
        fprintf(stderr, "[%d] S: +OK mailbox has %d messages (%d octets)\r\n", client_fd, num_msgs, total_msg_size);
    }

}

void process_NOOP(string argument, int client_fd, Pop3State& previousState){
    if (previousState != TRANSACTION) {
        string response = "-ERR command not allowed\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR command not allowed\n", client_fd);  
        }
        return;
    }

    if (!argument.empty()) {
        string response = "-ERR NOOP doesn't take any arguments\r\n";
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR NOOP doesn't take any arguments\n", client_fd);  
        }
        return;
    }

    string response = "+OK\r\n";
    if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: +OK\n", client_fd);  
    }

}

void process_QUIT(string argument, int client_fd, string mail_dir, Pop3State& previousState, string& user, map<string, bool>& deletion_flags){
    if (!argument.empty()) {
        string response = "-ERR no arguments allowed\r\n";
        cout<<"[S]: "<<response<<endl;
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: -ERR no arguments allowed\n", client_fd);  
        }
        return;
    }

    //If previous state is AUTH, exits
    if (previousState == AUTH) {
        string response = "+OK POP3 server signing off\r\n";
        // cout<<"[S]: "<<response<<endl;
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: +OK POP3 server signing off\n", client_fd);  
        }
        close(client_fd);
        pthread_exit(NULL);
    }

    //If previous state is TRANSACTION, deletes messages
    if (previousState == TRANSACTION) {
        previousState = UPDATE;

        string mbox_file_path = mail_dir + "/" + user + ".mbox";
        string temp_file_path = mail_dir + "/" + user + ".temp";

        pthread_mutex_lock(&fileMutexMap[mbox_file_path]);

        //Opens the original file and the temporary file
        int old_fd = open(mbox_file_path.c_str(), O_RDWR);
        if (old_fd < 0) {
            string response = "-ERR unable to access mailbox\r\n";
            // cout<<"[S]: "<<response<<endl;
            if (write(client_fd, response.c_str(), response.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR unable to access mailbox\n", client_fd);  
            }
            pthread_mutex_unlock(&fileMutexMap[mbox_file_path]);
            return;
        }
        flock(old_fd, LOCK_EX | LOCK_NB);

        ofstream temp_file(temp_file_path);
        ifstream old_file(mbox_file_path);

        if (!old_file.is_open() || !temp_file.is_open()) {
            string response = "-ERR unable to open mailbox\r\n";
            // cout<<"[S]: "<<response<<endl;
            if (write(client_fd, response.c_str(), response.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: -ERR unable to open mailbox\n", client_fd);  
            }
            close(old_fd);
            pthread_mutex_unlock(&fileMutexMap[mbox_file_path]);
            return;
        }

        //Reads old file message-by-message and writes to temp file if not deleted
        string line;
        string message;
        string copy_msg;
        // for (auto d: deletion_flags)
        // {
        //     cout<<"DEL FLAG: "<<d.first<<" "<<d.second<<endl;
        // }
        
        while (getline(old_file, line)) {
            if (line.rfind("From ", 0) == 0) {
                //Processes the previous message
                unsigned char digest[MD5_DIGEST_LENGTH];
                computeDigest(const_cast<char*>(message.c_str()), message.size(), digest);
                string hash = digestToHexString(digest, MD5_DIGEST_LENGTH);
                // cout<<"Hash: "<<hash<<endl;
                if (!deletion_flags[hash]) {
                    temp_file << copy_msg;
                }
                message.clear();
                copy_msg.clear();
            }
            else{
                message += line + "\n";
            }
            copy_msg  += line + "\n";
        }
        //Processes the last message
        if (!message.empty()) {
            unsigned char digest[MD5_DIGEST_LENGTH];
            computeDigest(const_cast<char*>(message.c_str()), message.size(), digest);
            string hash = digestToHexString(digest, MD5_DIGEST_LENGTH);
            // cout<<"Hash: "<<hash<<endl;
            if (!deletion_flags[hash]) {
                temp_file << copy_msg;
            }
        }

        old_file.close();
        temp_file.close();

        //Renames temp file to old file
        rename(temp_file_path.c_str(), mbox_file_path.c_str());

        flock(old_fd, LOCK_UN);
        close(old_fd);
        pthread_mutex_unlock(&fileMutexMap[mbox_file_path]);

        string response = "+OK POP3 server signing off\r\n";
        
        if (write(client_fd, response.c_str(), response.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: +OK POP3 server signing off\n", client_fd);  
        }
        close(client_fd);
        pthread_exit(NULL);
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
        cout<<"[S]: "<<shutdown_message<<endl;
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