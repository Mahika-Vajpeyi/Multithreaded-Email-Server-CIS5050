#ifndef EMAIL_H
#define EMAIL_H

#include <string>
#include <iostream>
#include <vector>

//Declares verbose as extern so it can access the definition from smtp.cc
extern bool verbose;

class Email {
public:
    // Enum to represent different states of the email session
    enum EmailState {
        INIT,  
        HELO,  
        QUIT,  
        RSET, 
        MAIL,  
        RCPT,  
        DATA,  
        NOOP,  
        DOT    
    };

private:
    std::string mailFrom;                   
    std::vector<std::string> rcptTo;        
    std::string data;                       
    EmailState previousState;               

public:
    // Constructor
    Email(std::string sender, std::string recipient, std::string emailData);

    // Setter and Getter for mailFrom
    void setMailFrom(const std::string& sender);
    std::string getMailFrom() const;

    // Setter and Getter for rcptTo
    void addRcptTo(const std::string& recipient);
    std::vector<std::string> getRcptTo() const;

    // Setter and Getter for data
    void setData(const std::string& emailData);
    std::string getData() const;

    // Setter and Getter for previousState
    void setPreviousState(const EmailState state);
    EmailState getPreviousState() const;

    // Method to display the email information
    void displayEmailInfo();

    void process_HELO(const std::string& domain, int client_fd); 
    void process_MAILFROM(const std::string& sender, int client_fd);
    void process_RCPTTO(const std::string& recipient, int client_fd, std::string mail_dir);

    bool isValidEmail(const std::string& email) const;

    void process_DATA(int& client_fd, std::string argument, std::string mail_dir);
    void process_RSET(int& client_fd, std::string argument);
    void process_QUIT(int& client_fd, std::string argument);
    void process_NOOP(int& client_fd, std::string argument);
};

#endif 
