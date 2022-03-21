# Online-File-Collaborator
Online File collaborator with multiple client sharing different files with different permission accesses built using socket APIs of C Language.

## Connection Phase
The server creates a socket and listens for client connections on a speciﬁc port. Client sends a request for connection to the server, and an appropriate reply is received based on success or failure.
* If successful, the server generates a unique 5-digit ID w.r.t. client socket id, and stores the client details in a client records ﬁle/data structure. A welcome message is returned to the client.
* If unsuccessful, either because more than 5 clients are
currently active or some other reason, an error message is returned to the client.
 * Server can service multiple clients at the same time using select() / fork()
## Server Functionalities
After successful connection, a client can now access different functionalities by sending queries to the server.
* ### Viewing active clients
  A client can request the server about the details of all active (currently connected) clients. The server returns a list of client IDs.
  ```
  /users
  ```
* ### Uploading a ﬁle
  A client can upload a local ﬁle to the server. The server will create its own copy of the same ﬁle, and return success / failure messages. If same name file is present at the server, server returns a failure message to client asking to upload file with different name.
    ```
    /upload <filename>
    ```
* ### Providing access to another client
  A client can allow access to other clients to any ﬁle it owns. If a client (say C1) asks the server to provide access to another client (say C2) to a particular ﬁle (say f) , the server dispatches an invite to C2. If C2 accepts the invitation, C2 becomes a collaborator of f . This invite (and subsequently on acceptance, the collaborator C2 itself) can be of two types, based on the type of access C1 wishes to give to C2:
   * Viewer (V): Read-only privilege, C2 will not be able to modify f
       ```
       /invite <filename> <client_id> V
      ```
   * Editor (E): Read+write privilege, C2 can modify f
     ```
       /invite <filename> <client_id> E
       ```
* ### Invites from other clients
  If C2 accepts the invite, the server should perform the necessary actions
   * Successful Invite: If the invite is successful, the server sends a success messages to both C1 and C2, and maintain track of collaborators (and their access privilege V/E) along with owners in the permission records ﬁle.
   * Unsuccessful Invite: An invite can fail in many scenarios (ﬁle does not exist, C1 is not the owner, C2 declines the invite, etc.). The server sends appropriate failure messages to C1 (and depending on the situation, to C2 also).
* ### Reading lines from a ﬁle 
  A client can request to read lines from a ﬁle by sending a query to the server. A client may be allowed to access the ﬁle (if it exists) only if it is the owner or a collaborator (either viewer or editor) of the requested ﬁle.
        ```
       /read <filename> <start_idx> <end_idx>
        ```
  Read from ﬁle ﬁlename starting from line index start_idx to end_idx . If only one index is speciﬁed, read that line. If none are speciﬁed, read the entire ﬁle.
  * Successful Read: If the read is successful, the requested line(s) are returned to the client.
  * Unsuccessful Read: Appropriate error message is returned to client based on situation (ﬁle does not exist / client does not have access / invalid line numbers)
 * ### Inserting lines to a ﬁle
   A client can request to insert line(s) into a ﬁle by sending a query to the server. A client may be allowed to modify the ﬁle (if it exists) only if it is the owner or a collaborator (editor only) of the requested ﬁle.
   * Successful Delete: Return the entire contents of the modiﬁed ﬁle
   * Unsuccessful Insert: Appropriate error message to be returned to client based on situation (ﬁle does not exist / client does not have access / invalid line number) 
## Client Functionalities
Take command input from user, parse the command, validate it and send appropriate query message to the server
