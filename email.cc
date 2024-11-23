#include <iostream>
#include <string>
#include <algorithm>
#include <unistd.h>
#include <cstring> 
#include <sys/socket.h>
#include "email.h"
#include <map>
#include <fstream>      
#include <pthread.h>    
#include <ctime>     
#include <iomanip>      
#include <sstream>  
#include <sys/file.h>

using namespace std;

//Declares verbose as extern so it can access the definition from smtp.cc
extern bool verbose;

//Global map for file mutexes
map<string, pthread_mutex_t> fileMutexMap;

//Checks if file path exists
bool fileExists(const string& filePath) {
    ifstream file(filePath);
    return file.good();
}

//Email constructor
Email::Email(string sender, string recipient, string emailData) {
    mailFrom = sender;
    if (!recipient.empty()) {
        rcptTo.push_back(recipient);
    }
    data = emailData;
    previousState = EmailState::INIT;
}

//Setter and Getter for mailFrom
void Email::setMailFrom(const string& sender) {
    mailFrom = sender;
}

string Email::getMailFrom() const {
    return mailFrom;
}

//Setter and Getter for rcptTo
void Email::addRcptTo(const std::string& recipient) {
    rcptTo.push_back(recipient);
}

vector<string> Email::getRcptTo() const {
    return rcptTo;
}

//Setter and Getter for data
void Email::setData(const string& emailData) {
    data = emailData;
}

string Email::getData() const {
    return data;
}

//Setter and Getter for previousState
void Email::setPreviousState(const EmailState state) {
    previousState = state;
}

Email::EmailState Email::getPreviousState() const {
    return previousState;
}

//Displays email information
void Email::displayEmailInfo() {
    cout << "Mail From: " << mailFrom << endl;
    for(int i = 0; i < rcptTo.size(); i++){
        cout << "Recipient: " << rcptTo[i] << endl;
    }
    cout << "Email Data: " << data << endl;
    cout << "Previous State: " << previousState << endl;
}

void Email::process_HELO(const string& domain, int client_fd) {
    //Prints error if domain is missing
    if (domain.empty()) {
        string message = "501: Syntax error - missing domain\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  // Verbose: Server ready message
        }
        return;
    }           
    else if (previousState == INIT || previousState == HELO) {
        //If previous state is INIT or HELO, set to HELO
        previousState = HELO;
        string message = "250 localhost\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // return;
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 250 localhost\n", client_fd);  // Verbose: Server ready message
        }
        return;
    } else {
        string message = "503 Bad sequence of commands\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 503 bad sequence of commands\n", client_fd);  // Verbose: Server ready message
        }
        return;
    }
}

void Email::process_MAILFROM(const string& sender, int client_fd) {
    if (previousState == HELO) {
        size_t colonPos = sender.find(':');
        if (colonPos != string::npos) {
            //Splits the string on ':'
            string command = sender.substr(0, colonPos);
            string addressPart = sender.substr(colonPos + 1);
            // cout << "command " << command << endl;
            // cout << "addressPart " << addressPart << endl;

            //Trims leading and trailing whitespace from command
            size_t cmdStart = command.find_first_not_of(" \t");
            size_t cmdEnd = command.find_last_not_of(" \t");
            if (cmdStart == string::npos || cmdEnd == string::npos) {
                string message = "501 Syntax error\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd); 
                }
                return;
            }
            command = command.substr(cmdStart, cmdEnd - cmdStart + 1);

            //Converts command to uppercase for case-insensitive comparison
            transform(command.begin(), command.end(), command.begin(), ::toupper);

            //Validates that the command is 'FROM'
            if (command != "FROM") {
                string message = "501 Syntax error\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }

            //Trims leading and trailing whitespace from addressPart
            size_t addrStart = addressPart.find_first_not_of(" \t");
            size_t addrEnd = addressPart.find_last_not_of(" \t");

            if (addrStart == string::npos || addrEnd == string::npos) {
                string message = "501 Syntax error\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }
            string address = addressPart.substr(addrStart, addrEnd - addrStart + 1);

            //Checks if address is enclosed in '<' and '>'
            if (address.front() != '<' || address.back() != '>') {
                string message = "501 Syntax error\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }

            //Extracts the email address between '<' and '>'
            string email = address.substr(1, address.size() - 2);

            //Validates the email format (basic validation)
            if (!isValidEmail(email)) {
                string message = "501 Syntax error: invalid email\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error-invalid email\n", client_fd);  
                }
                return;
            }

            //Sets mailFrom and updates the state
            mailFrom = email;
            previousState = MAIL;

            string message = "250 OK\r\n";
            if (write(client_fd, message.c_str(), message.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: 250 OK\n", client_fd);  
            }
        } else {
            //':' not found in sender string
            string message = "501 Syntax error: missing :\r\n";
            if (write(client_fd, message.c_str(), message.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
                // exit(1);
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
            }
        }
    } else {
        string message = "503 Bad sequence of commands\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 503 Bad sequence of commands\n", client_fd);  
        }
    }
}

void Email::process_RCPTTO(const string& recipient, int client_fd, string mail_dir) {
    if (previousState == MAIL || previousState == RCPT) {
        //Checks if recipient contains ':'
        size_t colonPos = recipient.find(':');
        if (colonPos != string::npos) {
            string command = recipient.substr(0, colonPos);
            string addressPart = recipient.substr(colonPos + 1);

            //Trims leading and trailing whitespace from command
            size_t cmdStart = command.find_first_not_of(" \t");
            size_t cmdEnd = command.find_last_not_of(" \t");
            if (cmdStart == string::npos || cmdEnd == string::npos) {
                string message = "501 Syntax error - wrong command format\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }
            command = command.substr(cmdStart, cmdEnd - cmdStart + 1);

            //Converts command to uppercase for case-insensitive comparison
            transform(command.begin(), command.end(), command.begin(), ::toupper);

            //Validates that the command is 'TO'
            if (command != "TO") {
                string message = "501 Syntax error - missing TO\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }

            //Trims leading and trailing whitespace from addressPart
            size_t addrStart = addressPart.find_first_not_of(" \t");
            size_t addrEnd = addressPart.find_last_not_of(" \t");
            if (addrStart == string::npos || addrEnd == string::npos) {
                string message = "501 Syntax error - incorrect address\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }
            string address = addressPart.substr(addrStart, addrEnd - addrStart + 1);

            //Checks if address is enclosed in '<' and '>'
            if (address.front() != '<' || address.back() != '>') {
                string message = "501 Syntax error - malformed email\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }

            //Extracts the email address between '<' and '>'
            string email = address.substr(1, address.size() - 2);

            //Checks if the email address ends with @localhost
            if (email.find("@localhost") == string::npos) {
                string message = "550 No such user\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 550 No such user\n", client_fd);  
                }
                return;
            }

            if (!isValidEmail(email)) {
                string message = "501 Syntax error - invalid email\r\n";
                if (write(client_fd, message.c_str(), message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    // exit(1);
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
                }
                return;
            }
            size_t atPos = email.find('@');
            string username = email.substr(0, atPos);
            
            string mbox_file_path = mail_dir + "/" + username + ".mbox";

            if (!fileExists(mbox_file_path)) {
                //If recipient file cannot be opened, sends error response
                string error_message = "550 Requested action not taken: mailbox unavailable\r\n";
                if (write(client_fd, error_message.c_str(), error_message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 550 Requested action not taken: mailbox unavailable\n", client_fd);  
                }
                return;
            }

            //Appends recipient to the rcptTo vector and update the state
            rcptTo.push_back(email);
            previousState = RCPT;

            // Respond with success
            // printf("250 OK\n");
            string message = "250 OK\r\n";
            if (write(client_fd, message.c_str(), message.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
                // exit(1);
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: 250 OK\n", client_fd);  
            }
            return;
        }
        else {
            //':' not found in recipient string
            string message = "501 Syntax error - missing :\r\n";
            if (write(client_fd, message.c_str(), message.length()) < 0) {
                fprintf(stderr, "Could not communicate with client\r\n");
                // exit(1);
            }
            if (verbose) {
                fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
            }
            return;
        }
    }
    else {
        //Invalid sequence of commands
        string message = "503 Bad sequence of commands\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 503 Bad sequence of commands\n", client_fd);  
        }
        return;
    }
}

bool Email::isValidEmail(const string& email) const {
    //Basic validation: checks for presence of '@'
    size_t atPos = email.find('@');
    // size_t dotPos = email.find('.', atPos);
    return (atPos != string::npos); //&& (dotPos == string::npos);
}

void Email::process_DATA(int& client_fd, string argument, string mail_dir) {
    //Checks if argument is not empty
    if (!argument.empty()) {
        const char* syntax_error_msg = "501 Syntax error: Remove argument\r\n";
        if (write(client_fd, syntax_error_msg, strlen(syntax_error_msg)) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
        }
        return;
    }

    if (previousState == RCPT) {
        //Prompts the user to start entering email data
        string message = "354 Start mail input; end with <CRLF>.<CRLF>\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 354 Start mail input; end with <CRLF>.<CRLF>\n", client_fd);  
        }

        string emailData;
        char recv_buffer[1024];
        ssize_t bytes_received;
        bool end_of_data = false;
        while (true) {
            //Receives data from the client
            bytes_received = recv(client_fd, recv_buffer, sizeof(recv_buffer), 0);
            if (bytes_received == -1) {
                fprintf(stderr, "read failed\n");
                return;
            }
            else if (bytes_received == 0) {
                //Connection closed by client
                fprintf(stderr, "Client disconnected\n");
                return;
            }

            //Appends the received data to emailData
            emailData.append(recv_buffer, bytes_received);

            //Checks if the termination sequence\r\n.\r\n is present
            size_t term_pos = emailData.find("\r\n.\r\n");
            if (term_pos != string::npos) {
                //Removes the termination sequence from emailData
                emailData.erase(term_pos);
                break;
                // end_of_data = true;
            }
        }
        if(!emailData.empty() && emailData.back()!='\n'){
            emailData.append("\r\n");
        }
        data = emailData;
        previousState = DATA;
        
        string success_message = "250 OK\r\n";
        if (write(client_fd, success_message.c_str(), success_message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 250 OK\n", client_fd);  
        }

        for (const string& recipient : rcptTo) {
            //Creates the header of the form: From <sender's email> <current timestamp> <LF>
            time_t now = time(0);
            struct tm* now_tm = localtime(&now);
            stringstream ss;
            ss << put_time(now_tm, "%a %b %d %H:%M:%S %Y");
            string formatted_time = ss.str();
            string header = "From <" + mailFrom + "> " + formatted_time + "\n";

            //Acquires the mutex for the recipient's mbox file
            if (fileMutexMap.find(recipient) == fileMutexMap.end()) {
                //Initializes the mutex if it does not exist
                pthread_mutex_t newMutex;
                pthread_mutex_init(&newMutex, nullptr);
                fileMutexMap[recipient] = newMutex;
            }
            pthread_mutex_t& fileMutex = fileMutexMap[recipient];

            //Locks the mutex
            pthread_mutex_lock(&fileMutex);

            //Opens the recipient's mbox file
            size_t atPos = recipient.find('@');
            string username = recipient.substr(0, atPos);
            
            string mbox_file_path = mail_dir + "/" + username + ".mbox";
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

            ofstream mboxFile(mbox_file_path, ios::app);

            if (mboxFile.is_open()) {
                mboxFile << header;
                mboxFile << emailData;
                mboxFile.close();
            } else {
                //If recipient file cannot be opened, sends error response
                string error_message = "550 Requested action not taken: mailbox unavailable\r\n";
                if (write(client_fd, error_message.c_str(), error_message.length()) < 0) {
                    fprintf(stderr, "Could not communicate with client\r\n");
                    //Does not exit, process the remaining recipients
                }
                if (verbose) {
                    fprintf(stderr, "[%d] S: 550 Requested action not taken: mailbox unavailable\n", client_fd);  
                }
                cerr << "Failed to open mbox file for recipient: " << recipient << "\n";
            }

            //Unlocks the file
            flock(old_fd, LOCK_UN);
            close(old_fd);
            // Unlock the mutex
            pthread_mutex_unlock(&fileMutex);

        }
    } else {
        //Invalid sequence of commands
        string message = "503 Bad sequence of commands\r\n";
        if (write(client_fd, message.c_str(), message.length()) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 503 Bad sequence of commands\n", client_fd);  
        }
    }
}

void Email::process_RSET(int& client_fd, string argument) {
    //Checks if an argument is provided
    if (!argument.empty()) {
        const char* syntax_error_msg = "501 Syntax error: Remove argument\r\n";
        if (write(client_fd, syntax_error_msg, strlen(syntax_error_msg)) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
        }
        return;
    }

    //Checks if the previous state is INIT
    if (previousState == INIT) {
        const char* bad_sequence_msg = "503 Bad sequence of commands\r\n";
        if(write(client_fd, bad_sequence_msg, strlen(bad_sequence_msg)) < 0){
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 503 Bad sequence of commands\n", client_fd);  
        }
        return;
    }

    //Clears all stored sender, recipients, and mail data
    mailFrom.clear();
    rcptTo.clear();
    data.clear();
    previousState = INIT;

    //Sends 250 OK to the client
    const char* success_msg = "250 OK\r\n";
    if(write(client_fd, success_msg, strlen(success_msg)) < 0){
        fprintf(stderr, "Could not communicate with client\r\n");
        // exit(1);
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: 250 OK\n", client_fd);  
    }
}

void Email::process_QUIT(int& client_fd, string argument) {
    //Checks if an argument is provided
    if (!argument.empty()) {
        const char* syntax_error_msg = "501 Syntax error: Remove argument\r\n";
        if (write(client_fd, syntax_error_msg, strlen(syntax_error_msg)) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
        }
        return;
    }

    // Check if the previous state is not HELO
    // if (previousState != HELO) {
    //     const char* bad_sequence_msg = "503 Bad sequence of commands\r\n";
    //     if(write(client_fd, bad_sequence_msg, strlen(bad_sequence_msg)) < 0){
    //         fprintf(stderr, "Could not communicate with client\r\n");
    //         exit(1);
    //     }
    //     return;
    // }

    //Sends 250 OK to the client
    const char* success_msg = "221 localhost closing transmission\r\n";
    if(write(client_fd, success_msg, strlen(success_msg)) < 0){
        fprintf(stderr, "Could not communicate with client\r\n");
        // exit(1);
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: 221 localhost closing transmission\n", client_fd);  
    }

    //Clears all stored sender, recipients, and mail data
    mailFrom.clear();
    rcptTo.clear();
    data.clear();
    previousState = INIT;

    //Closes the client connection
    close(client_fd);
    pthread_exit(nullptr);
}

void Email::process_NOOP(int& client_fd, string argument) {
    //Checks if an argument is provided
    if (!argument.empty()) {
        const char* syntax_error_msg = "501 Syntax error: Remove argument\r\n";
        if (write(client_fd, syntax_error_msg, strlen(syntax_error_msg)) < 0) {
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 501 Syntax error\n", client_fd);  
        }
        return;
    }

    //Checks if the previous state is not INIT
    if (previousState != INIT) {
        const char* bad_sequence_msg = "503 Bad sequence of commands\r\n";
        if(write(client_fd, bad_sequence_msg, strlen(bad_sequence_msg)) < 0){
            fprintf(stderr, "Could not communicate with client\r\n");
            // exit(1);
        }
        if (verbose) {
            fprintf(stderr, "[%d] S: 503 Bad sequence of commands\n", client_fd);  
        }
        return;
    }

    //Sends 250 OK to the client
    const char* success_msg = "250 OK\r\n";
    if(write(client_fd, success_msg, strlen(success_msg)) < 0){
        fprintf(stderr, "Could not communicate with client\r\n");
        // exit(1);
    }
    if (verbose) {
        fprintf(stderr, "[%d] S: 250 OK\n", client_fd);  
    }
}