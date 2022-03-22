#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/wait.h>
#include <ctype.h>
#include <netinet/tcp.h>
#define PORT "5690"

char central_file[10000000];
int client_id;

//Function to read data of size recv_bytes
int read_data(int sock_fd, char *buffer, size_t buff_size, size_t recv_bytes)
{
    int n=0, no_of_bytes_received = 0;
    bzero(buffer, buff_size);
    do
    {
        n = read(sock_fd, buffer+no_of_bytes_received, recv_bytes);
        if (n < 0)
            return n;

        //n contains no of bytes read.
        no_of_bytes_received += n;
        recv_bytes -= n;
    } while (no_of_bytes_received < recv_bytes);
    return 1;
}

//Function to write data of size write_bytes
int write_data(int sock_fd, char *buffer, size_t bytes_to_send)
{
    int n;
    size_t Remaining_Bytes_to_send = bytes_to_send;
    while (Remaining_Bytes_to_send != 0)
    {
        n = write(sock_fd, buffer, Remaining_Bytes_to_send);
        if (n < 0)
            return n;

        //n contains the no. of bytes sent
        Remaining_Bytes_to_send -= n;
        strcpy(buffer, buffer + n);
    }
    return 1;
}

char *strtok_ro(char *buffer, char *delim, char **rest)
{
    //To Tokensize string with single delim.
    char *token = buffer;
    if (buffer[0] == '\0')
    {
        //buffer is empty
        *rest = token;
        return token;
    }
    int i = 1;
    for (; buffer[i] != *delim && buffer[i] != '\0'; i++)
        ;

    *rest = token + i;
    if (buffer[i] != '\0')
    {
        *rest = *rest + 1;
        buffer[i] = '\0';
    }
    return token;
}

void error(char *str)
{
    //error handling function
    perror(str);
    exit(1);
}

void invalid()
{
    //TO PRINT Invalid Format of command entered by USER.
    printf("------------------------------------------------Invalid command\n");
}

size_t file_size(FILE *fp)
{
    //To Determine the file size
    fseek(fp, 0L, SEEK_END);
    size_t s = ftell(fp);
    return s;
}

bool file_exist_and_readable(char *File_name)
{
    //To check if the file exist or not
    if (access(File_name, F_OK | R_OK) == 0)
        return true;
    return false;
}

int upload_one_file(int sock_fd, FILE *fp, size_t fs)
{
    //Uploading one file to the server
    rewind(fp);
    bzero(central_file, sizeof(central_file));
    fread(central_file,sizeof(char),fs,fp);

    int n = write_data(sock_fd, central_file, fs);
    return n;
}

bool Isinteger(char *str)
{
    for(int i = (str[0] == '-') ;str[i] != '\0';i++)
    {
        if(!isdigit(str[i]))
        {
            return false;
        }
    }
    return true;
}

void print(int n)
{
    printf("Reached at line %d\n",n);
}

int clear_input_buffer()
{
    char ch;
    int n = 0;
    while((ch = getchar()) != '\n' && ch != EOF)
        n++;
    return n;

}

void copy_from_central(char *buffer, size_t buff_size, char *temp, size_t temp_size)
{
    bzero(buffer,buff_size);
    strcpy(buffer,central_file);

    bzero(temp,temp_size);
    strcpy(temp,buffer);
}

bool handle_command(int sock_fd)
{
    int n,spaces = 0;
    size_t co;
    size_t Bytes_to_send = 50, Bytes_to_recv = 50;
    char buffer[100], temp[100], ch, *last_space_pointer;
    //read command from the stdin

    bzero(central_file,sizeof(central_file));
    fgets(central_file,sizeof(central_file),stdin);
    co = strlen(central_file)*sizeof(char);

    //count no of spaces
    for(int i = 0;central_file[i] != '\0';i++)
    {
        if(central_file[i] == '"')
            break;
        if(central_file[i] == ' ')
        {
            spaces++;
            last_space_pointer = central_file + i + 1;
        }
    }

    FILE *fp;
    char *token, *rest;

    //Insert command needs to be handled seperately as message can contain new line character.
    //command needs to be: /insert <file_Name> <idx> "--message--" OR /insert <file_Name> "--message--"
    if(strncmp(central_file,"/insert",7) == 0)
    {
        //command: /insert
        if(spaces < 1 || spaces > 3 || *last_space_pointer != '"')
        {
            //invalid command
            invalid();
            return false;
        }
        
        //take the input until the 2nd last character before '"' is detected.
        while(central_file[strlen(central_file) - 2] != '"')
        {
            fgets(central_file+co,sizeof(central_file)-co,stdin);
            co = strlen(central_file)*sizeof(char);
        }
        
        //last character will be '\n', remove it
        central_file[strlen(central_file) - 1] = '\0';

        //tokenize and process the command
        token = strtok_ro(central_file," ",&rest);          //token: "/insert"

        char file_name_buffer[20];
        bzero(buffer,sizeof(buffer));
        strcpy(buffer,"/insert ");

        size_t msg_size;

        token = strtok_ro(rest," ",&rest);                  //token: "<file_Name>"
        strcpy(file_name_buffer,token);
        strcat(buffer,token);                           //Appending file name
        strcat(buffer," ");

        if(*rest == 0)
        {
            invalid();
            return false;
        }

        if(*rest == '"')
        {
            //only message is given
            strcat(buffer,"0 ");
        }
        else
        {
            //index is given
            token = strtok_ro(rest," ",&rest);
            if(!Isinteger(token))
            {
                invalid();
                return false;
            }
            else
            {
                strcat(buffer,"1 ");
                strcat(buffer,token);
                strcat(buffer," ");
            }
        }
        //command format is correct

        /*----------Custom command: /insert <file_Name> 0 <msg size> OR /insert <filename> 1 <index> <msg size>---------------*/

        //'rest' is pointing to the first '"' of message
        
        rest[strlen(rest)-1] = '\0';               //Removing last '"'
        rest++;                                     //skiping the first '"'
        
        msg_size = strlen(rest)*sizeof(char); 

        bzero(temp,sizeof(temp));
        sprintf(temp,"%zu",msg_size);

        strcat(buffer,temp);

        //write command to the server
        n = write_data(sock_fd,buffer,Bytes_to_send);
        if(n < 0)
            return true;
        
        //wait for server response
        n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
        if(n < 0)
            return true;

        if (strcmp(buffer, "File Not Found") == 0)
        {
            printf("File Not Found\n");
        }
        else if (strcmp(buffer, "Not Accessible") == 0)
        {
            printf("You don't have write permission on file '%s'\n",file_name_buffer);
        }
        else if(strcmp(buffer,"Invalid Index") == 0)
        {
            printf("Invalid Index\n");
        }
        else
        {
            //server has accepted the command as valid.

            n = write_data(sock_fd,rest,msg_size);
            if(n < 0)
                return true;

            //read the updated file

            //first read the file size
            n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
            if(n < 0)
                return true;
            sscanf(buffer,"%zu",&msg_size);

            //now read file of size 'msg_size'
            n = read_data(sock_fd,central_file,sizeof(central_file),msg_size);
            if(n < 0)
                return true;
            printf("\nThe Update file content is:\n%s\n",central_file);
        }

        return false;
    }

    //command is not insert so, can be processed normally without taking new line as input.
    central_file[strlen(central_file)-1] = '\0';
    copy_from_central(buffer,sizeof(buffer),temp,sizeof(temp));

    //Normal Process
    token = strtok_ro(temp, " ", &rest);
    if (strcmp(buffer, "/exit") == 0)
    {
        //command: /exit
        n = write_data(sock_fd, buffer, Bytes_to_send);
        return true; //Irrespective of n, client gonna terminate.
    }
    else if (strcmp(buffer, "/users") == 0)
    {
        //command: /users
        n = write_data(sock_fd, buffer, Bytes_to_send);
        if (n < 0)
            return true;
        
        //server will first send the data size that will be transmitted
        n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
        if(n < 0)
            return true;
        sscanf(buffer,"%zu",&Bytes_to_recv);

        //wait for server to reply back with all active clients
        n = read_data(sock_fd, central_file, sizeof(central_file), Bytes_to_recv);
        if (n < 0)
            return true;
        printf("List of Active clients: \n%s\n", central_file);
    }
    else if (strcmp(buffer, "/files") == 0)
    {
        //command: /files
        n = write_data(sock_fd, buffer, Bytes_to_send);
        if (n < 0)
            return true;

        //wait for server to reply back with all files with all it's details

        //server will first send the size of the data
        bzero(buffer, sizeof(buffer));
        n = read_data(sock_fd, buffer, sizeof(buffer), Bytes_to_recv);
        if (n < 0)
            return true;
        
        if(strcmp(buffer,"No file") == 0)
        {
            printf("There are no files at the server side\n");
            return false;
        }
        sscanf(buffer, "%zu", &Bytes_to_recv);

        //Now server will send the file
        n = read_data(sock_fd, central_file, sizeof(central_file), Bytes_to_recv);
        if (n < 0)
            return true;

        printf("%s\n", central_file);
    }
    else if (*rest != 0 && strcmp(token, "/upload") == 0)
    {
        //command: /upload <file_name>
        token = strtok_ro(rest, " ", &rest);
        //File name cannot spaces
        if (*rest != 0)
        {
            invalid();
        }
        else if (!file_exist_and_readable(token))
        {
            //File doesn't exist or doesn't have read permission
            printf("File '%s' doesn't exist or doesn't have read permission\n",token);
        }
        else
        {
            //command: /upload file_name file_size
            //file exists and have read permission

            fp = fopen(token, "r+");
            if (fp == NULL)
            {
                printf("Error in opening file\n");
                return false;
            }

            size_t fs = file_size(fp);
            // printf("File size to be sent: %zu\n",fs);
            bzero(temp, sizeof(temp)); //using temp buffer to store file size string
            sprintf(temp, "%zu", fs);

            strcat(buffer, " ");
            strcat(buffer, temp);

            //send this command to the server.
            n = write_data(sock_fd, buffer, Bytes_to_send);
            if (n < 0)
            {
                fclose(fp);
                return true;
            }

            //server will check if the file can be uploaded
            n = read_data(sock_fd, buffer,sizeof(buffer),Bytes_to_recv);
            if (n < 0)
            {
                fclose(fp);
                return true;
            }

            if (strcmp(buffer, "OK") == 0)
            {
                //client can start uploading
                n = upload_one_file(sock_fd, fp, fs);
                if (n < 0)
                {
                    fclose(fp);
                    return true;
                }
                printf("File uploaded\n");
            }
            else
            {
                //Try with different file name
                printf("Try with different file name\n");
                fclose(fp);
            }
        }
    }
    else if (*rest != 0 && strcmp(token, "/download") == 0)
    {
        //command: \download <file_name>
        token = strtok_ro(rest, " ", &rest);
        //File name cannot contain spaces
        if (*token == ' ' || *rest != 0)
        {
            invalid();
        }
        else
        {
            //command: /download file_name

            //send command to server
            n = write_data(sock_fd, buffer, Bytes_to_send);
            if (n < 0)
                return true;

            //wait for server response
            n = read_data(sock_fd, buffer, sizeof(buffer), Bytes_to_recv);
            if (n < 0)
                return true;

            if (strcmp(buffer, "File Not Found") == 0)
            {
                printf("File Not Found\n");
            }
            else if (strcmp(buffer, "Not Accessible") == 0)
            {
                printf("File '%s' is not accessible by you\n",token);
            }
            else
            {
                //File size that server now gonna send
                sscanf(buffer, "%zu", &Bytes_to_recv);

                //Read data sent by server
                n = read_data(sock_fd, central_file, sizeof(central_file), Bytes_to_recv);
                if (n < 0)
                    return true;

                //Make a file and store data that in it at client side
                fp = fopen(token, "w+");
                if (fp == NULL)
                {
                    printf("Error in create file\n");
                    return false; //don't terminate client
                }
                fwrite(central_file,sizeof(char),Bytes_to_recv,fp);

                fclose(fp);
                fp = NULL;
                printf("FILE Downloaded\n");
            }
        }
    }
    else if (*rest != 0 && strcmp(token, "/invite") == 0)
    {
        //command: /invite <file_name> <client_ID> <permission>

        //check if owner is inviting itself.
        int invitee;
        token = strtok_ro(rest," ",&rest);
        char *file_Name = token,*client_name;
        if(*token == ' ' || *rest == 0)
            invalid();
        else
        {
            token = strtok_ro(rest," ",&rest);
            client_name = token;
            sscanf(client_name,"CS%d",&invitee);
            if(invitee == client_id)
            {
                //client is inviting itself
                printf("You are inviting yourself :|\n");
            }
            else if(*token == ' ' || *rest == 0)
                invalid();
            else
            {
                token = strtok_ro(rest," ",&rest);
                if(*rest != 0 || (*token != 'V' && *token != 'E'))
                    invalid();
                else
                {
                    //command format is correct

                    //send it to the server.
                    n = write_data(sock_fd,buffer,Bytes_to_send);
                    if(n < 0)
                        return true;

                    //wait for server's response
                    n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
                    if(n < 0)
                        return true;

                    if (strcmp(buffer, "File Not Found") == 0)
                    {
                        printf("File Not Found\n");
                    }
                    else if (strcmp(buffer, "Not Accessible") == 0)
                    {
                        printf("You don't own this file so, can't send invite\n");
                    }
                    else if(strcmp(buffer,"Client Not found") == 0)
                    {
                        printf("Client Not found\n");
                    }
                    else if(strcmp(buffer,"Already permitted") == 0)
                    {
                        printf("Client '%s' has already has this or higher permission for file '%s'\n",client_name,file_Name);
                    }
                    else if(strcmp(buffer,"Downgraded") == 0)
                    {
                        printf("Access of file by client '%s' has been downgraded\n",client_name);
                    }
                    else
                    {
                        printf("Invite Request sent. Response will be notified\n");
                    }
                }
            }
        }
    }
    else if (*rest != 0 && strcmp(token, "/read") == 0)
    {
        char file_name_buffer[20];
        bzero(buffer,sizeof(buffer));
        strcpy(buffer,"/read ");
        token = strtok_ro(rest," ",&rest);
        strcpy(file_name_buffer,token);
        strcat(buffer,token);               //Appending file name
        strcat(buffer," "); 

        if(*rest != 0)
        {
            //One or Both indices are specified
            token = strtok_ro(rest," ",&rest);
            if(*rest == 0)
            {
                //Only index is specified
                if(!Isinteger(token))
                {
                    print(410);
                    invalid();
                    return false;
                }
                else
                {
                    strcat(buffer,"1 ");
                    strcat(buffer,token);
                }
            }
            else
            {
                //Both Indices are specified
                if(!Isinteger(token) || !Isinteger(rest))
                {
                    print(425);
                    invalid();
                    return false;
                }
                else
                {
                    strcat(buffer,"2 ");
                    strcat(buffer,token);
                    strcat(buffer," ");
                    strcat(buffer,rest);
                }
            }
        }
        else
        {
            //No index is specified
            strcat(buffer,"0");
        }

        //command format is correct, send custom command to the server
        n = write_data(sock_fd,buffer,Bytes_to_send);
        if(n < 0)
            return true;
        
        //wait for server response
        n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
        if(n < 0)
            return true;

        if (strcmp(buffer, "File Not Found") == 0)
        {
            printf("File Not Found\n");
        }
        else if (strcmp(buffer, "Not Accessible") == 0)
        {
            printf("You don't have read/write permission on the file '%s'\n",file_name_buffer);
        }
        else if(strcmp(buffer,"Invalid Index") == 0)
        {
            printf("Invalid Index\n");
        }
        else
        {
            //server has sent the line size
            sscanf(buffer,"%zu",&Bytes_to_recv);

            n = read_data(sock_fd,central_file,sizeof(central_file),Bytes_to_recv);
            if(n < 0)
                return true;
            
            //line(s) received, print it
            printf("Content on the file between given range: \n%s\n",central_file);
        }
    }
    else if(*rest != 0 && strcmp(token,"/delete")==0)
    {
        char file_name_buffer[20];
        bzero(buffer,sizeof(buffer));
        strcpy(buffer,"/delete ");
        token = strtok_ro(rest," ",&rest);
        strcpy(file_name_buffer,token);
        strcat(buffer,token);               //Appending file name
        strcat(buffer," ");

        if(*rest != 0)
        {
            //One or Both indices are specified
            token = strtok_ro(rest," ",&rest);
            if(*rest == 0)
            {
                //Only index is specified
                if(!Isinteger(token))
                {
                    invalid();
                    return false;
                }
                else
                {
                    strcat(buffer,"1 ");
                    strcat(buffer,token);
                }
            }
            else
            {
                //Both Indices are specified
                if(!Isinteger(token) || !Isinteger(rest))
                {
                    invalid();
                    return false;
                }
                else
                {
                    strcat(buffer,"2 ");
                    strcat(buffer,token);
                    strcat(buffer," ");
                    strcat(buffer,rest);
                }
            }
        }
        else
        {
            //No index is specified
            strcat(buffer,"0");
        }

        //command format is correct, send custom command to the server
        n = write_data(sock_fd,buffer,Bytes_to_send);
        if(n < 0)
            return true;
        
        //wait for server response
        n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
        if(n < 0)
            return true;

        if (strcmp(buffer, "File Not Found") == 0)
        {
            printf("File Not Found\n");
        }
        else if (strcmp(buffer, "Not Accessible") == 0)
        {
            printf("File '%s' is not accessible by you\n",file_name_buffer);
        }
        else if(strcmp(buffer,"Invalid Index") == 0)
        {
            printf("Invalid Index\n");
        }
        else
        {
            //server has sent the update file size
            size_t fs;
            sscanf(buffer,"%zu",&fs);

            //receive the file size
            n = read_data(sock_fd,central_file,sizeof(central_file),fs);
            if(n < 0)
                return true;
            printf("\nThe Updated file content:\n%s\n",central_file);
        }
    }
    else
    {
        invalid();
    }
    return false;
}

bool handle_invites(int sock_fd)
{
    //To Handle invite requests and response of earlier invite.
    size_t Bytes_to_send = 50, Bytes_to_recv = 50;
    char copy_buffer[100], buffer[100],*token, *rest, *file_name, *client_ID, *permission,*file_key;
    int n;
    n = read_data(sock_fd,buffer,sizeof(buffer),Bytes_to_recv);
    if(n < 0)
        return true;
    
    bzero(copy_buffer,sizeof(buffer));
    strcpy(copy_buffer,buffer);

    //tokenizing the invite request
    printf("\33[2K\r");
    token = strtok_ro(copy_buffer," ",&rest);
    if(strcmp(token,"/invite") == 0)
    {
        printf("Invitation Request received, Press ENTER to view details-----");
        fflush(stdout);
        clear_input_buffer();

        //Invite request: /invite <file_name> <clien_ID> <permission> <file_key>
        file_name = strtok_ro(rest, " ",&rest);             //filename

        client_ID = strtok_ro(rest," ",&rest);               //Client ID of client sending collaborator request              

        permission = strtok_ro(rest," ",&rest);             //permission

        file_key = strtok_ro(rest," ",&rest);           //file_key

        printf("Invitation for file '%s' as ",file_name);
        if(*permission == 'E')
        {
            printf("editor");
        }
        else
        {
            printf("viewer");
        }
        printf(" from '%s'\n",client_ID);
        printf("Enter 'Y' to accept the request. Anything else entered will reject the request: ");
        fflush(stdout);
        
        //now take the input   
        char res;     
        scanf("%c",&res);

        //discard of other characters
        n = clear_input_buffer();
        if(n != 0)
            res = 'N';

        //send back the custom response commmand to the server

        //command: /response <client_ID> <file_name> <file_key> <permission> <A/R>
        bzero(buffer, sizeof(buffer));
        strcpy(buffer, "/response ");

        strcat(buffer, client_ID);
        strcat(buffer, " ");

        strcat(buffer, file_name);
        strcat(buffer, " ");

        strcat(buffer, file_key);
        strcat(buffer, " ");

        strcat(buffer,permission);
        strcat(buffer, " ");

        if(res == 'Y')
        {
            strcat(buffer,"A");
        }
        else
        {
            strcat(buffer,"R");
        }

        n = write_data(sock_fd,buffer,Bytes_to_send);
        if(n < 0)
            return true;
        
        if(res == 'Y')
        {
            //wait for server confirmation
            n = read_data(sock_fd, buffer, sizeof(buffer), Bytes_to_recv);
            if (n < 0)
                return true;

            if (strcmp(buffer, "Client Not found") == 0)
            {
                printf("\nClient '%s' exits the system.\n",client_ID);
            }
            else if (strcmp(buffer, "Granted") == 0)
            {
                printf("\n\tPermission Granted\n");
            }
            else
            {
                printf("\nError in giving access permissions\n");
            }
        }
        else
        {
            printf("\n\tInvite Request declined\n");
        }
    }
    else if(strcmp(token,"/invite_reply") == 0)
    {
        printf("Invitation Response received, Press ENTER to view details-----");
        fflush(stdout);
        clear_input_buffer();

        //response of previous invitation sent.
        //command: /invite_reply <file_name> <clientID> <permission> <A/R>

        file_name = strtok_ro(rest," ",&rest);          //file_name

        client_ID = strtok_ro(rest," ",&rest);      //client ID

        permission = strtok_ro(rest," ",&rest);     //permission

        char acc = *(strtok_ro(rest," ",&rest));      //A or R

        //print the response
        printf("Invitation to client '%s' as ",client_ID);
        if(*permission == 'V')
        {
            printf("viewer ");
        }
        else
        {
            printf("Editor ");
        }
        printf("for file '%s' is ",file_name);
        if(acc == 'A')
        {
            //accepted
            printf("accepted\n");
        }
        else 
        {
            //rejected
            printf("rejected\n");
        }
    }
    return false;
}

int main()
{
    int sock_fd, n;
    struct addrinfo hints, *server_info, *p;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, PORT, &hints, &server_info) != 0)
        error("Error on finding server\n");

    int y = 1;
    for (p = server_info; p != NULL; p = p->ai_next)
    {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            continue;
        }
        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sock_fd);
            continue;
        }
        break;
    }
    freeaddrinfo(server_info);
    if (p == NULL)
    {
        error("Error in Initializing and connecting to the socket");
    }
    char buffer[100];
    size_t buff_size = sizeof(buffer);
    /*-----------------------check if server terminated the connection--------------------*/
    bzero(buffer, sizeof(buffer));
    n = read_data(sock_fd, buffer, buff_size, 20);
    if (n < 0)
    {
        close(sock_fd);
        error("Error in Reading\n");
    }
    if (strcmp(buffer, "LIMIT REACHED") == 0)
    {
        printf("Server is at MAX Limit.....please try again after some time.\n");
        return 0;
    }
    else
    {
        //connected
        sscanf(buffer,"%d",&client_id);
        printf("Connected with the server as the client 'CS%d'\n",client_id);
    }

    /*---------------------To wait on STDIN and sock_fd simultaneously------------------------*/

    fd_set read_fds; //master file descriptor list, Temporary File descriptor list for select
    int fdmax;       //for storing max file descriptor

    while (1)
    {
        bzero(buffer, buff_size);
        printf("\nclient 'CS%d'[+]: ",client_id);
        fflush(stdout);

        FD_ZERO(&read_fds); //clearing socket file descriptor set

        //set the STDIN descriptor
        FD_SET(STDIN_FILENO, &read_fds);

        //set the sock_fd descriptor
        FD_SET(sock_fd, &read_fds);

        fdmax = sock_fd;

        //wait for any activity on STDIN or sock_fd
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            FD_ZERO(&read_fds);
            close(sock_fd);
            error("Error on select\n");
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            //STDIN is active i.e. client entered something
            if (handle_command(sock_fd))
            {
                //client terminates
                close(sock_fd);
                FD_ZERO(&read_fds);
                return 0;
            }
        }
        else if (FD_ISSET(sock_fd, &read_fds))
        {
            //received a collaboration request on the socket or response to a previous invite received
            if(handle_invites(sock_fd))
            {
                //Error incurred with the client
                close(sock_fd);
                FD_ZERO(&read_fds);
                return 0;
            }

        }
    }
    return 0;
}