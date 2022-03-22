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
#include <time.h>
#include <netinet/tcp.h>
#include <limits.h>
#define PORT "5690"
#define MAX_CLIENT 5

int connected = 0;

char central_file[1000000];
int file_key_global;

typedef struct permission
{
    int client_ID;
    char perm;
}permission;

typedef struct file
{
    char file_name[30];
    int owner_ID, file_key;
    permission collaborators[MAX_CLIENT];
    struct file *next_file, *prev_file;
} file;

typedef struct client
{
    int sock_fd, client_ID;
} client;


void error(char *str)
{
    //error handling function
    perror(str);
    exit(1);
}

void print(int n)
{
    printf("Reached at line %d\n",n);
}

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
        buffer = buffer + n;
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

bool check_file_empty(FILE *fptr)
{
    //check for empty file
    fseek(fptr, 0, SEEK_END);
    int s = ftell(fptr);
    if (s == 0)
        return true;
    return false;
}

int NLINEX(FILE *fptr)
{
    //To check for the number of lines in the file
    rewind(fptr);
    if (check_file_empty(fptr))
    {
        //File is empty
        return 0;
    }
    rewind(fptr);
    int n = 0;
    char c;
    c = fgetc(fptr);
    while (c != EOF)
    {
        //increment n when newline character is encountered
        if (c == '\n')
            n++;
        c = fgetc(fptr);
    }
    //At the end of the last line, we will not find newline character so, increment n by once.
    n++;
    return n;
}

int upload_file(int sock_fd, FILE *fptr, size_t fs)
{
    int n = read_data(sock_fd, central_file, sizeof(central_file), fs);
    if (n < 0)
        return n;

    fwrite(central_file,sizeof(char),fs,fptr);
    return 1;
}

int accessible_by_client(client *clients, char *file_Name, file *file_head)
{
    //find out whether or not this client can access this file or not
    if (!file_exist_and_readable(file_Name))
    {
        //file not found
        return -1;
    }
    int ret = -1;
    file *iter;
    for (iter = file_head; iter != NULL; iter = iter->next_file)
    {
        if (strcmp(iter->file_name, file_Name) == 0)
        {
            ret = 0;
            if (iter->owner_ID == clients->client_ID)
                return 3;

            //check if file has client as viewer or editor collaborator
            for (int i = 0; i < MAX_CLIENT; i++)
            {
                if(iter->collaborators[i].client_ID == clients->client_ID)
                {
                    //client is a collaborator
                    if(iter->collaborators[i].perm == 'V')
                        return 1;
                    else
                        return 2;
                }
            }
            break;
        }
    }
    return ret;

    // -1   ---->   File not found
    //  0   ---->   File not accessible
    //  1   ---->   viewer access
    //  2   ---->   Editor access
    //  3  ----->   Owner
}

int send_entrire_file(int sock_fd, FILE *fptr)
{
    size_t fs = file_size(fptr);
    rewind(fptr);
    bzero(central_file, sizeof(central_file));
    fread(central_file, sizeof(char), fs, fptr);

    //writing file data on to the buffer
    int n = write_data(sock_fd, central_file, fs);
    return n;
}

bool check_valid_index(FILE *fptr, int loc)
{
    int no_of_lines = NLINEX(fptr);
    if (loc < -no_of_lines || loc >= no_of_lines)
        return false;
    return true;
}

int read_file(FILE *fptr, int start_idx, int end_idx)
{
    // printf("start index = %d, end_index = %d\n",start_idx,end_idx);
    bzero(central_file, sizeof(central_file));
    if (start_idx == INT_MIN)
    {
        //read entire file
        size_t fs = file_size(fptr);
        rewind(fptr);
        fread(central_file, sizeof(char), fs, fptr);
        return 1;
    }

    int no_of_lines = NLINEX(fptr);
    start_idx = (start_idx + no_of_lines) % no_of_lines;
    end_idx = (end_idx + no_of_lines) % no_of_lines;

    if(start_idx > end_idx)
        return 0;

    rewind(fptr);
    //skip all lines [0 to start_idx-1]
    char ch;
    for (int i = 0; i < start_idx; i++)
    {
        while ((ch = fgetc(fptr)) != '\n');              //ch cannot become EOF as it would've get flagged as invalid index
    }

    int count = 0;
    for (int i = start_idx; i <= end_idx; i++)
    {
        do
        {
            ch = fgetc(fptr);
            if(ch == EOF)
                break;
            central_file[count++] = ch;
        } while (ch != '\n');
    }
    return 1;
}

int delete_from_files(FILE *fptr, int start_idx, int end_idx, char *file_name)
{
    
    if (start_idx == INT_MIN)
    {
        //delete entire file content
        freopen(file_name, "w+", fptr);
    }
    else
    {
        rewind(fptr);
        char ch;
        char temp[100] = "temp_";

        //make a new temp file
        while (file_exist_and_readable(temp))
        {
            strcat(temp, "1");
        }
        FILE *fp_temp = fopen(temp, "w+"); //Assuming file is created

        //move fp to the location 'loc'
        int no_of_lines = NLINEX(fptr);
        start_idx = (start_idx + no_of_lines) % no_of_lines;
        end_idx = (end_idx + no_of_lines) % no_of_lines;
        if(start_idx > end_idx)
            return 0;
        // printf("start index: %d, end index: %d\n",start_idx,end_idx);

        rewind(fptr);
        for (int i = 0; i < start_idx; i++)
        {
            do
            {
                ch = fgetc(fptr);
                fputc(ch, fp_temp);
            } while (ch != '\n');               //ch cannot be EOF else it would've get flagged as invalid index
        }

        
        if(end_idx == no_of_lines-1)
        {
            //[start_index to last line] needs to be deleted.
            int pos = ftell(fptr)-1;            //-1: To delete '\n' character.
            if(pos < 0)
            {
                //start index is 0, then pos will be -1
                pos++;
            }
            ftruncate(fileno(fptr),pos);
            remove(temp);
            return 1;
        }
        //skip lines [start_end to end_idx]
        for (int j = start_idx; j <= end_idx; j++)
        {
            do
            {
                ch = fgetc(fptr);
                // printf("%c",ch);
            } while (ch != EOF && ch != '\n');
        }
        while (ch != EOF && (ch = fgetc(fptr)) != EOF)
        {
            // printf("%c", ch);
            fputc(ch, fp_temp);
        }

        //move entire content of file from fp_temp to fptr
        freopen(file_name, "w+", fptr);
        rewind(fp_temp);

        while ((ch = fgetc(fp_temp)) != EOF)
        {
            fputc(ch, fptr);
        }

        //removing temporary file
        remove(temp);
    }
    return 1;
}

client *client_exist(client *clients, char *c_id)
{
    if (c_id[0] != 'C' || c_id[1] != 'S')
        return NULL;
    for (int i = 2; c_id[i] != '\0'; i++)
    {
        if (!isdigit(c_id[i]))
            return NULL;
    }
    int check_c_id;
    sscanf(c_id + 2, "%d", &check_c_id);                //skipping 'CS' part
    for (int i = 0; i < MAX_CLIENT; i++)
    {
        if (clients[i].client_ID == check_c_id)
            return clients + i;
    }
    return NULL;
}

void insert_in_file(FILE *fptr, int loc, char *file_name)
{
    size_t fs = strlen(central_file)*sizeof(char);
    if (loc == INT_MAX)
    {
        fseek(fptr, 0, SEEK_END);
        //insert received message in central_file buffer at the fptr
        fputc('\n', fptr);
        fwrite(central_file,sizeof(char),fs,fptr);
    }
    else
    {
        char ch;
        char temp[100] = "temp_";

        //make a new non-existing temp file
        while (file_exist_and_readable(temp))
        {
            strcat(temp, "1");
        }
        FILE *fp_temp = fopen(temp, "w+"); //Assuming file is created

        //move fp to the location 'loc'
        int no_of_lines = NLINEX(fptr);
        loc = (loc + no_of_lines)%no_of_lines;
        rewind(fptr);
        for (int i = 0; i < loc; i++)
        {
            do
            {
                ch = fgetc(fptr);
                fputc(ch, fp_temp);
            } while (ch != '\n');           //ch will never become EOF else it would have been flaaged as invalid index
        }
        fwrite(central_file,sizeof(char),fs,fp_temp);
        fputc('\n',fp_temp);
        while ((ch = fgetc(fptr)) != EOF)
        {
            fputc(ch, fp_temp);
        }

        //move entire content of file from fp_temp to fptr
        freopen(file_name, "w+", fptr);
        rewind(fp_temp);

        while ((ch = fgetc(fp_temp)) != EOF)
        {
            
            fputc(ch, fptr);
        }

        //removing temporary file
        remove(temp);
    }
}

int give_file_key(file *file_head, char *file_Name)
{
    file *iter;
    for (iter = file_head; iter != NULL; iter = iter->next_file)
    {
        if (strcmp(iter->file_name, file_Name) == 0)
            break;
    }
    return iter->file_key;
}

void set_permission(file **file_head, int invitee, char *file_Name, char permission)
{
    //function to set or upgrade the permission from 'V' to 'E'
    file *iter;
    for (iter = *file_head; iter != NULL; iter = iter->next_file)
    {
        if (strcmp(file_Name, iter->file_name) == 0)
        {
            if (permission == 'V')
            {
                //invitee must not have viewer permission and editor permission
                for (int j1 = 0; j1 < MAX_CLIENT; j1++)
                {
                    if (iter->collaborators[j1].client_ID == -1)
                    {
                        iter->collaborators[j1].client_ID = invitee;         //setting view permission to the invitee
                        iter->collaborators[j1].perm = 'V';
                        return;
                    }
                }
            }
            else
            {
                //permission: 'E'

                //remove 'V' access of invitee, if exists
                for (int j1 = 0; j1 < MAX_CLIENT; j1++)
                {
                    if (iter->collaborators[j1].client_ID == invitee && iter->collaborators[j1].perm == 'V')
                    {
                        iter->collaborators[j1].perm = 'E';
                        return;
                    }
                }
                //add 'E' permisssion.
                for (int j1 = 0; j1 < MAX_CLIENT; j1++)
                {
                    if (iter->collaborators[j1].client_ID == -1)
                    {
                        iter->collaborators[j1].client_ID = invitee;
                        iter->collaborators[j1].perm = 'E';
                        return;
                    }
                }
            }
        }
    }
}

file *create_file_node(char *file_Name, int c_id)
{
    file *new_node = (file *)malloc(sizeof(file));
    strcpy(new_node->file_name, file_Name);
    new_node->owner_ID = c_id;
    new_node->file_key = file_key_global++;
    memset(new_node->collaborators,-1,sizeof(new_node->collaborators));
    new_node->prev_file = new_node->next_file = NULL;
    return new_node;
}

void Insert_in_file_LL(file **file_head, file *new_node)
{
    file *iter;
    if (*file_head == NULL)
    {
        //first node is getting inserted
        *file_head = new_node;
        return;
    }
    //move to the last element
    for (iter = *file_head; iter->next_file != NULL; iter = iter->next_file)
        ;

    iter->next_file = new_node;
    new_node->prev_file = iter;
}

void delete_all_file_owned_by(file **file_head, client *cl)
{
    file *iter = *file_head;
    while (iter != NULL)
    {
        if (iter->owner_ID == cl->client_ID)
        {
            //this file is owned by the client cl
            remove(iter->file_name);

            //delete the node
            if (iter->prev_file == NULL)
            {
                //first node
                if (iter->next_file == NULL)
                {
                    //only one node
                    *file_head = NULL;
                }
                else
                {
                    *file_head = iter->next_file;
                    (*file_head)->prev_file = NULL;
                }
                free(iter);
                iter = *file_head;
            }
            else if (iter->next_file == NULL)
            {
                //last node
                //but can't be first node as it is being handled in the previous case

                iter->prev_file = NULL;
                free(iter);
                iter = NULL;
            }
            else
            {
                file *tem = iter->next_file;
                iter->prev_file->next_file = iter->next_file;
                iter->next_file->prev_file = iter->prev_file;

                free(iter);
                iter = tem;
            }
        }
        else
        {
            //cl is not owing this file

            //if cl has any permission on this then revoke it
            for(int i = 0;i<MAX_CLIENT;i++)
            {
                if(iter->collaborators[i].client_ID == cl->client_ID)
                {
                    //cl is a collaborator of this file
                    iter->collaborators[i].client_ID = -1;      //remove it's access
                }
            }
            iter = iter->next_file;
        }
            
    }
}

void downgrade_from_E_to_V(file **file_head, char *file_Name, client *client_invited)
{
    file *iter;
    for(iter = *file_head;iter!=NULL;iter = iter->next_file)
    {
        if(strcmp(file_Name,iter->file_name) == 0)
        {
            //removing 'E' access
            for(int i = 0;i<MAX_CLIENT;i++)
            {
                if(iter->collaborators[i].client_ID == client_invited->client_ID)
                {
                    iter->collaborators[i].perm = 'V';                  //Downgrading
                    break;
                }
            }
        }
    }
}

bool match_f_key(file *file_head, char *file_Name, int f_key)
{
    file *iter;
    for(iter = file_head;iter!=NULL;iter = iter->next_file)
    {
        if(strcmp(iter->file_name,file_Name) == 0 && iter->file_key == f_key)
            return true;
    }
    return false;
}

bool perform_operation(client *clients, int idx, file **file_head)
{
    char copy_buffer[100], buffer[100], *token, *rest, *client_ID, *file_name, *permission, *file_k, temp1[55], temp2[55];
    size_t Bytes_to_send = 50, Bytes_to_recv = 50, buff_size = sizeof(buffer), fs;
    file *iter;
    int n;
    FILE *fptr = NULL;

    
    n = read_data(clients[idx].sock_fd, buffer, buff_size, Bytes_to_recv);
    if (n < 0)
    {
        delete_all_file_owned_by(file_head, clients + idx);
        return true;
    }

    //make a copy of this command
    bzero(copy_buffer, buff_size);
    strcpy(copy_buffer, buffer);

    //decode the command
    token = strtok_ro(buffer, " ", &rest);
    if (strcmp(token, "/exit") == 0)
    {
        //command: /exit            Client is exiting

        //delete all it's file
        delete_all_file_owned_by(file_head, clients + idx);
        return true;
    }
    else if (strcmp(token, "/users") == 0)
    {
        //command: /users

        //send back the list of all active clients

        bzero(central_file,sizeof(central_file));
        for (int i = 0; i < MAX_CLIENT; i++)
        {
            fflush(stdout);
            if (clients[i].client_ID != -1)
            {
                bzero(buffer,buff_size);
                sprintf(buffer,"\tCS%d",clients[i].client_ID);

                strcat(central_file,buffer);
            }
        }
        bzero(buffer, buff_size);
        fs = strlen(central_file)*sizeof(char);
        sprintf(buffer,"%zu",fs);

        //first send the size of the data that will be transmitted
        n = write_data(clients[idx].sock_fd,buffer,Bytes_to_send);
        if(n < 0)
        {
            delete_all_file_owned_by(file_head, clients + idx);
            return true;
        }
        
        n = write_data(clients[idx].sock_fd, central_file, fs);
        if (n < 0)
        {
            delete_all_file_owned_by(file_head, clients + idx);
            return true;
        }
    }
    else if (strcmp(token, "/files") == 0)
    {
        //command: /files

        //send back the list of all files present at the server side along with their details
        bzero(central_file, sizeof(central_file));
        // printf("file head: %p\n",*file_head);
        if (*file_head == NULL)
        {
            strcpy(central_file, "No file");
            n = write_data(clients[idx].sock_fd, central_file, Bytes_to_send);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
        }
        else
        {
            bool All_files_not_opened = true;
            for (iter = *file_head; iter != NULL; iter = iter->next_file)
            {
                //first open the file
                fptr = fopen(iter->file_name, "r+");
                if (fptr == NULL)
                    continue;
                else
                    All_files_not_opened = false;

                //Appending the file name
                strcat(central_file, "\nFile Name: ");
                strcat(central_file, iter->file_name);
                strcat(central_file, "\n");

                //Appending the owner name
                strcat(central_file, "Owner: ");

                bzero(temp1,sizeof(temp1));
                sprintf(temp1, "CS%d", iter->owner_ID);
                strcat(central_file, temp1);
                strcat(central_file, "\n");

                //finding the number of lines in the file
                int no_of_lines = NLINEX(fptr);
                bzero(copy_buffer, sizeof(copy_buffer));
                sprintf(copy_buffer, "Number of lines: %d\n", no_of_lines);
                strcat(central_file,copy_buffer);
                fclose(fptr);
                fptr = NULL;

                //Appending collaborators
                strcat(central_file, "Collaborator:---------\n");
                for (int j = 0; j < MAX_CLIENT; j++)
                {
                    if(iter->collaborators[j].client_ID != -1)
                    {
                        bzero(copy_buffer, sizeof(copy_buffer));
                        if(iter->collaborators[j].perm == 'E')
                        {
                            //this file pointed by iter has a editor
                            sprintf(copy_buffer, "\tCS%d\tEditor\n", iter->collaborators[j].client_ID);
                        }
                        else
                        {
                            //this file pointed by iter has a viewer
                            sprintf(copy_buffer, "\tCS%d\tViewer\n", iter->collaborators[j].client_ID);
                        }
                        strcat(central_file, copy_buffer);
                    }
                }
            }

            //check if all of the file went opened
            if (All_files_not_opened)
            {
                bzero(copy_buffer, sizeof(copy_buffer));
                strcpy(copy_buffer, "No file");
                n = write_data(clients[idx].sock_fd, copy_buffer, Bytes_to_send);
                if (n < 0)
                {
                    delete_all_file_owned_by(file_head, clients + idx);
                    return true;
                }
                return false;
            }

            //central file has the file details opened

            //first send the file size
            bzero(copy_buffer, sizeof(copy_buffer));
            fs = strlen(central_file) * sizeof(char);
            sprintf(copy_buffer, "%zu", fs);

            n = write_data(clients[idx].sock_fd, copy_buffer, Bytes_to_send);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }

            //Now send the data
            n = write_data(clients[idx].sock_fd, central_file, fs);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
        }
    }
    else if (strcmp(token, "/upload") == 0)
    {
        //command: /upload file_name file_size
        file_name = strtok_ro(rest, " ", &rest);
        bzero(temp1, sizeof(temp1));
        strcpy(temp1, file_name);
        if (file_exist_and_readable(temp1))
        {
            //file with same name exist, try with different name
            bzero(buffer, buff_size);
            strcpy(buffer, "NOK");
            n = write_data(clients[idx].sock_fd, buffer, Bytes_to_send);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
        }
        else
        {
            //size of file thats gonna get uploaded
            token = strtok_ro(rest, " ", &rest);
            sscanf(token, "%zu", &fs);
            // printf("size of file to be received: %zu\n",fs);

            //file can be uploaded
            bzero(buffer, buff_size);
            strcpy(buffer, "OK");
            n = write_data(clients[idx].sock_fd, buffer, Bytes_to_send);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }

            //start receiving file
            fptr = fopen(temp1, "w+");
            n = upload_file(clients[idx].sock_fd, fptr, fs);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }

            //file has been created whose file pointer is 'fptr'
            file *new_node = create_file_node(temp1, clients[idx].client_ID);
            Insert_in_file_LL(file_head, new_node);
            fclose(fptr);
        }
    }
    else if (strcmp(token, "/download") == 0)
    {
        file_name = strtok_ro(rest, " ", &rest);
        strcpy(temp1, file_name);

        int acc = accessible_by_client(clients + idx, temp1, *file_head);

        bzero(buffer, buff_size);
        if (acc == -1)
        {
            //file not found
            strcpy(buffer, "File Not Found");
        }
        else if (acc == 0)
        {
            //File not accessible
            strcpy(buffer, "Not Accessible");
        }
        else
        {
            //file accessible

            //find the file size
            fptr = fopen(temp1, "r+");
            if (fptr == NULL)
            {
                strcpy(buffer, "File Not Found");
            }
            else
            {
                //file opened
                fs = file_size(fptr);
                sprintf(buffer, "%zu", fs);
            }
        }

        //first send this response to the client
        n = write_data(clients[idx].sock_fd, buffer, Bytes_to_send);
        if (n < 0)
        {
            delete_all_file_owned_by(file_head, clients + idx);
            return true;
        }

        if (acc > 0 && strcmp(buffer,"File Not Found") != 0)
        {
            //file found, accessible, and opened successfully
            n = send_entrire_file(clients[idx].sock_fd, fptr);
            fclose(fptr);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
        }
    }
    else if (strcmp(token, "/invite") == 0)
    {
        //command: /invite <file_name> <client_ID> <permission>
        bzero(copy_buffer,buff_size);
        strcpy(copy_buffer,"/invite ");
        int acc = -1;
        token = strtok_ro(rest, " ", &rest);
        strcpy(temp1, token);
        strcat(copy_buffer,temp1);
        strcat(copy_buffer," ");

        //Attach client[idx] ID to the copy buffer
        bzero(temp2, sizeof(temp2));
        sprintf(temp2,"CS%d ",clients[idx].client_ID);
        strcat(copy_buffer,temp2);

        if (!file_exist_and_readable(temp1))
        {
            bzero(buffer, buff_size);
            strcpy(buffer, "File Not Found");
        }
        else
        {
            acc = accessible_by_client(clients + idx, temp1, *file_head);
            //acc will be not -1 as it is FILE not found which is already being handled in the 'if' case
            if (acc != 3 && acc != -1)
            {
                bzero(buffer, buff_size);
                strcpy(buffer, "Not Accessible");
            }
            else
            {
                //file is owned by the client[idx], so invite is possible

                //first check if the client is there or not.
                token = strtok_ro(rest, " ", &rest);

                client *client_invited = client_exist(clients, token);

                if (client_invited != NULL)
                {
                    //client exist so, sent the custom request to c_id

                    //check if the client_invited has already have the same permission or not
                    int ac2 = accessible_by_client(client_invited,temp1,*file_head);
                    if(*rest == 'V' && ac2 == 1)
                    {
                        //checking the client_invited already have the 'V' permission
                        bzero(buffer, buff_size);
                        strcpy(buffer, "Already permitted");
                    }
                    else if (*rest == 'V' && ac2 == 2)
                    {
                        //downgrade the access of the client_invited
                        downgrade_from_E_to_V(file_head,temp1,client_invited);

                        //notify the invitee
                        /*---------*/


                        /*---------*/
                        bzero(buffer,buff_size);
                        strcpy(buffer,"Downgraded");
                    }
                    else if(*rest == 'E' && ac2 == 2)
                    {
                        bzero(buffer, buff_size);
                        strcpy(buffer, "Already permitted");
                    }
                    else
                    {
                        //client_invited don't have any permission or have V permission and invitation to make 'E'

                        //attach permission to the copy_buffer
                        strcat(copy_buffer,rest);
                        strcat(copy_buffer," ");

                        //find that file key
                        int f_key = give_file_key(*file_head, temp1);

                        bzero(temp2, sizeof(temp2));
                        sprintf(temp2, "%d", f_key);
                        strcat(copy_buffer, temp2);

                        //send this to the client invited
                        n = write_data(client_invited->sock_fd, copy_buffer, Bytes_to_send);
                        if (n < 0)
                        {
                            //client invited seems unreachable,
                            bzero(buffer, buff_size);
                            strcpy(buffer, "Client Not found");

                            //we are not terminating client_invited even still if it is unreachable.
                        }
                        else
                        {
                            bzero(buffer, buff_size);
                            strcpy(buffer, "Invite sent");
                        }
                    }
                }
                else
                {
                    //client doesn't exist
                    bzero(buffer, buff_size);
                    strcpy(buffer, "Client Not found");
                }
            }
        }

        //sent back the response to the invitor
        n = write_data(clients[idx].sock_fd, buffer, Bytes_to_send);
        if (n < 0)
        {
            delete_all_file_owned_by(file_head, clients + idx);
            return true;
        }
    }
    else if (strcmp(token, "/read") == 0)
    {
        //custom command: /read <filename> <0/1/2> <s_idx> <e_idx>
        int acc = -1;
        // printf("command received: %s\n",rest);
        file_name = strtok_ro(rest, " ", &rest);
        strcpy(temp1, file_name);
        if (!file_exist_and_readable(temp1) || (fptr = fopen(temp1, "r+")) == NULL)
        {
            //file not found
            bzero(buffer, buff_size);
            strcpy(buffer, "File Not Found");
        }
        else
        {
            acc = accessible_by_client(clients + idx, temp1, *file_head);
            if (acc == -1)
            {
                bzero(buffer, buff_size);
                strcpy(buffer, "File Not Found");
            }
            else if (acc == 0)
            {
                bzero(buffer, buff_size);
                strcpy(buffer, "Not Accessible");
            }
            else
            {
                //file found and accessible
                int start_idx = INT_MIN, end_idx = INT_MAX;
                if (*rest == '1' || *rest == '2')
                {
                    //only one index is given
                    token = strtok_ro(rest, " ", &rest);

                    if (*token == '1')
                    {
                        token = strtok_ro(rest, " ", &rest);
                        sscanf(token, "%d", &start_idx);
                        end_idx = start_idx;
                    }
                    else
                    {
                        token = strtok_ro(rest, " ", &rest);
                        sscanf(token, "%d", &start_idx);

                        token = strtok_ro(rest, " ", &rest);
                        sscanf(token, "%d", &end_idx);
                    }
                }
                
                //check for invalid_index
                if (start_idx != INT_MIN && (!check_valid_index(fptr, start_idx) || !check_valid_index(fptr, end_idx)))
                {
                    //invalid_index
                    bzero(buffer, buff_size);
                    strcpy(buffer, "Invalid Index");
                }
                else
                {
                    n = read_file(fptr, start_idx, end_idx);
                    if(n == 0)
                    {
                        //invalid index
                        bzero(buffer, buff_size);
                        strcpy(buffer, "Invalid Index");
                    }
                    else
                    {
                        //required data is in central but first find the file size
                        bzero(buffer, buff_size);
                        fs = strlen(central_file) * sizeof(char);
                        sprintf(buffer, "%zu", fs);
                    }
                }
            }
            fclose(fptr);
            fptr = NULL;
        }
        // printf("acc = %d\n",acc);

        //sending response
        n = write_data(clients[idx].sock_fd, buffer, Bytes_to_send);
        if (n < 0)
        {
            delete_all_file_owned_by(file_head, clients + idx);
            return true;
        }

        if (acc > 0 && strcmp(buffer,"Invalid Index") != 0)
        {
            //file found, accessible and valid index
            //sending the file
            n = write_data(clients[idx].sock_fd, central_file, fs);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
        }
    }
    else if (strcmp(token, "/response") == 0)
    {
        //command: /response <client_ID> <file_name> <file_key> <permission> <A/R>

        //file will be not available only if invitor client is terminated
        char access_;

        token = strtok_ro(rest, " ", &rest);
        client *invitor = client_exist(clients, token);
        bzero(copy_buffer, buff_size);
        if (invitor == NULL)
        {
            //client doesn't exist
            strcpy(copy_buffer, "Client Not found");
        }
        else
        {
            //client found
            file_name = strtok_ro(rest, " ", &rest);
            bzero(temp1, sizeof(temp1));
            strcpy(temp1, file_name);

            token = strtok_ro(rest, " ", &rest); //file key
            int f_key;
            sscanf(token, "%d", &f_key);

            permission = strtok_ro(rest, " ", &rest); // 'V' or 'E'

            access_ = *(strtok_ro(rest, " ", &rest)); //Accept or Reject

            if(match_f_key(*file_head,temp1,f_key))
            {
                //file key matched, invitee is real
                if(access_ != 'R')
                {
                    //invitee accepted the invitation
                    set_permission(file_head, clients[idx].client_ID, temp1,*permission);
                    strcpy(copy_buffer,"Granted");
                }
            }
            else
            {
                //file key not matched

                //send this message to the fake invitee
                strcpy(copy_buffer, "NOK");
            }
        }
        if (access_ != 'R')
        {
            //invitee has been granted permission if it was real OR 'NOK' has been sent

            n = write_data(clients[idx].sock_fd, copy_buffer, Bytes_to_send);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
        }

        if (invitor != NULL && strcmp(copy_buffer, "NOK") != 0)
        {
            //invitor is active and invitee was real, notify invitor

            //command: /invite_reply <file_name> <clientID> <permission> <A/R>

            bzero(copy_buffer, buff_size);
            strcpy(copy_buffer, "/invite_reply ");

            strcat(copy_buffer, temp1);
            strcat(copy_buffer, " ");

            bzero(temp2, sizeof(temp2));
            sprintf(temp2, "CS%d ", clients[idx].client_ID);
            strcat(copy_buffer, temp2);

            strcat(copy_buffer, permission);
            strcat(copy_buffer, " ");

            copy_buffer[strlen(copy_buffer)] = access_; //access contains A or R

            //writing to the invitor
            n = write_data(invitor->sock_fd, copy_buffer, Bytes_to_send);
            /*----we are not terminating the invitor client if it is unreachable---*/
        }
    }
    else if (strcmp(token, "/insert") == 0)
    {
        int acc = -1, loc = INT_MAX;
        file_name = strtok_ro(rest, " ", &rest);
        strcpy(temp1, file_name);
        if (!file_exist_and_readable(temp1) || (fptr = fopen(temp1, "r+")) == NULL)
        {
            //file doesn't exist
            bzero(buffer, buff_size);
            strcpy(buffer, "File Not Found");
        }
        else
        {
            //file exist and readable
            acc = accessible_by_client(clients + idx, temp1, *file_head);
            if (acc == -1)
            {
                //file doesn't exist
                bzero(buffer, buff_size);
                strcpy(buffer, "File Not Found");
            }
            else if (acc == 0 || acc == 1)
            {
                //file not accessible
                bzero(buffer, buff_size);
                strcpy(buffer, "Not Accessible");
            }
            else
            {
                token = strtok_ro(rest, " ", &rest);
                if (*token == '1')
                {
                    //index provided.
                    token = strtok_ro(rest, " ", &rest);
                    sscanf(token, "%d", &loc);
                }
                token = strtok_ro(rest, " ", &rest);
                sscanf(token, "%zu", &fs);

                if (loc != INT_MAX && !check_valid_index(fptr, loc))
                {
                    bzero(buffer, buff_size);
                    strcpy(buffer, "Invalid Index");
                }
                else
                {
                    bzero(buffer, buff_size);
                    strcpy(buffer, "OK");
                }
            }
        }

        //send response back to client
        n = write_data(clients[idx].sock_fd, buffer, Bytes_to_send);
        if (n < 0)
        {
            delete_all_file_owned_by(file_head, clients + idx);
            return true;
        }

        if (acc >= 2 && strcmp(buffer,"Invalid Index") != 0)
        {
            //read message from client and insert at index loc
            n = read_data(clients[idx].sock_fd, central_file, sizeof(central_file), fs);
            if (n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }

            insert_in_file(fptr, loc, temp1);

            /*------------send back the update file-----------*/
            
            //first send file size
            fs = file_size(fptr);
            bzero(buffer,buff_size);
            sprintf(buffer,"%zu",fs);
            n = write_data(clients[idx].sock_fd,buffer,Bytes_to_send);
            if(n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }

            //now send the file
            n = send_entrire_file(clients[idx].sock_fd,fptr);
            if(n < 0)
            {
                delete_all_file_owned_by(file_head, clients + idx);
                return true;
            }
            /*--------------------------------------------------*/
        }
        if(fptr != NULL)
        {
            fclose(fptr);
            fptr = NULL;
        }
        
    }
    else if (strcmp(token, "/delete") == 0)
    {
        int acc = -1;
        file_name = strtok_ro(rest, " ", &rest);
        strcpy(temp1, file_name);
        if (!file_exist_and_readable(temp1) || (fptr = fopen(temp1, "r+")) == NULL)
        {
            //file not found
            bzero(buffer, buff_size);
            strcpy(buffer, "File Not Found");
        }
        else
        {
            acc = accessible_by_client(clients + idx, temp1, *file_head);
            if (acc == -1)
            {
                bzero(buffer, buff_size);
                strcpy(buffer, "File Not Found");
            }
            else if (acc == 0 || acc == 1)
            {
                bzero(buffer, buff_size);
                strcpy(buffer, "Not Accessible");
            }
            else
            {
                //file found and accessible
                int start_idx = INT_MIN, end_idx = INT_MAX;
                if (*rest == '1' || *rest == '2')
                {
                    //only one index is given
                    token = strtok_ro(rest, " ", &rest);

                    if (*token == '1')
                    {
                        token = strtok_ro(rest, " ", &rest);
                        sscanf(token, "%d", &start_idx);
                        end_idx = start_idx;
                    }
                    else
                    {
                        token = strtok_ro(rest, " ", &rest);
                        sscanf(token, "%d", &start_idx);

                        token = strtok_ro(rest, " ", &rest);
                        sscanf(token, "%d", &end_idx);
                    }
                }

                bzero(buffer, buff_size);
                if (start_idx != INT_MIN && (!check_valid_index(fptr, start_idx) || !check_valid_index(fptr, end_idx)))
                {
                    strcpy(buffer, "Invalid Index");
                }
                else
                {
                    n = delete_from_files(fptr, start_idx, end_idx,temp1);
                    if(n == 0)
                    {
                        strcpy(buffer, "Invalid Index");
                    }
                    else
                    {
                        //delete successful.
                        
                        //send the update file size as later we have to send the update file.
                        fs = file_size(fptr);
                        sprintf(buffer,"%zu",fs);
                    }
                }
            }
        }
        //sending back the response
        n = write_data(clients[idx].sock_fd,buffer,Bytes_to_send);
        if(n < 0)
        {
            delete_all_file_owned_by(file_head,clients+idx);
            return true;
        }
        if(acc >= 2 && strcmp(buffer,"Invalid Index") != 0)
        {
            //editor or owner, and valid index          file would've been update.
            n = send_entrire_file(clients[idx].sock_fd,fptr);
            if(n < 0)
            {
                delete_all_file_owned_by(file_head,clients+idx);
                return true;
            }
        }
    }

    return false;
}

int main()
{
    srand(time(0));
    file_key_global = rand();
    fd_set master;    //master file descriptor list
    int fdmax;        //for storing max file descriptor
    FD_ZERO(&master); //clearing master file descriptor list

    int listener, new_fd;                //listener socket, socket after accepting
    struct sockaddr_storage client_addr; //for client address

    struct addrinfo hints, *server_info, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;       //For using IPv4
    hints.ai_socktype = SOCK_STREAM; //For TCP
    hints.ai_flags = AI_PASSIVE;     //For using local machine IP address
    if (getaddrinfo(NULL, PORT, &hints, &server_info) != 0)
        error("Error on intializing server\n");

    //loop through all the results and bind with the first possible one
    int y = 1;
    for (p = server_info; p != NULL; p = p->ai_next)
    {
        if ((listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            continue;

        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int)) == -1)
        {
            error("Error on setting socket socket options\n");
        }
        if (bind(listener, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(listener);
            continue;
        }
        break;
    }
    freeaddrinfo(server_info);
    if (p == NULL)
        error("Error on creating socket or binding it\n");

    if (listen(listener, 5) < 0)
    {
        close(listener);
        error("Error on listening\n");
    }

    char buffer[20];
    int n, client_ID_counter = rand() % 900 + 100;

    //to keep track of the fdmax when a client close
    client clients[MAX_CLIENT];
    for (int i = 0; i < MAX_CLIENT; i++)
    {
        clients[i].sock_fd = -1;
        clients[i].client_ID = -1;
    }

    file *file_head = NULL;

    while (1)
    {
        //clearing socket descriptor set
        FD_ZERO(&master);

        //adding listener socket to the socket descriptor set
        FD_SET(listener, &master);

        fdmax = listener;

        //adding remaining client sockets to the socket descriptor set
        for (int j = 0; j < MAX_CLIENT; j++)
        {
            if (clients[j].sock_fd != -1)
                FD_SET(clients[j].sock_fd, &master);

            //update fdmax, if necessary
            if (fdmax < clients[j].sock_fd)
                fdmax = clients[j].sock_fd;
        }

        //waiting for the activity
        if (select(fdmax + 1, &master, NULL, NULL, NULL) == -1)
            error("Error on select\n");

        //new connection request
        if (FD_ISSET(listener, &master))
        {
            socklen_t addrlen = sizeof(client_addr);
            new_fd = accept(listener, (struct sockaddr *)&client_addr, &addrlen);
            if (new_fd == -1)
            {
                close(new_fd);
                perror("Error on accept\n");
            }
            else
            {
                if (connected == MAX_CLIENT)
                {
                    //Limit reached, reject the client
                    strcpy(buffer, "LIMIT REACHED");
                    n = write_data(new_fd, buffer, 20);
                    if (n < 0)
                    {
                        perror("Error in writing\n");
                    }
                    close(new_fd);
                }
                else
                {
                    printf("\n\t\t\tConnected to the new CLIENT CS%d\n\n", client_ID_counter);
                    sprintf(buffer, "%d", client_ID_counter);
                    n = write_data(new_fd, buffer, 20);
                    if (n < 0)
                    {
                        close(new_fd);
                        perror("Error in writing at 881\n");
                    }
                    connected++;
                    if (new_fd > fdmax)
                        fdmax = new_fd;

                    for (int j = 0; j < MAX_CLIENT; j++)
                    {
                        //serach for empty slot
                        if (clients[j].sock_fd == -1)
                        {
                            clients[j].sock_fd = new_fd;
                            clients[j].client_ID = client_ID_counter;
                            break;
                        }
                    }
                    client_ID_counter++;
                }
            }
            continue;
        }

        //otherwise it is data from existing client
        for (int j1 = 0; j1 < MAX_CLIENT; j1++)
        {
            if (clients[j1].client_ID != -1 && FD_ISSET(clients[j1].sock_fd, &master))
            {
                if (perform_operation(clients, j1, &file_head))
                {
                    //client terminates
                    connected--;
                    close(clients[j1].sock_fd);
                    clients[j1].sock_fd = -1;
                    clients[j1].client_ID = -1;
                    break;
                }
            }
        }
    }
    FD_ZERO(&master);
    return 0;
}