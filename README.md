# Multithreaded-Email-Server-CIS5050

This project has two multithreaded servers: an SMTP server, which can be used to send emails using a normal mail client, and a POP3 server, which can be used to retrieve emails. 

### SMTP Server

It implements the following commands specified in RFC 821(https://tools.ietf.org/html/rfc821):
• HELO <domain>, which starts a connection;
• MAIL FROM:, which tells the server who the sender of the email is;
• RCPT TO:, which specifies the recipient;
• DATA, which is followed by the text of the email and then a dot (.) on a line by itself;
• QUIT, which terminates the connection;
• RSET, which aborts a mail transaction; and
• NOOP, which does nothing.

### POP3 Server 
It implements the following commands specified in RFC 1939[https://tools.ietf.org/html/rfc1939]
• USER, which tells the server which user is logging in;
• PASS, which specifies the user's password;
• STAT, which returns the number of messages and the size of the mailbox;
• UIDL, which shows a list of messages, along with a unique ID for each message;
• RETR, which retrieves a particular message;
• DELE, which deletes a message;
• QUIT, which terminates the connection;
• LIST, which shows the size of a particular message, or all the messages;
• RSET, which undeletes all the messages that have been deleted with DELE; and
• NOOP, which does nothing.
