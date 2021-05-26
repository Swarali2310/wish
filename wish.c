#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <wait.h>
#include <errno.h>
#define ELEMENTS 50
#define ARRAY_SIZE 50
#define PATH_SIZE 10

//frees all the memory that was allocated on the heap before user presses Ctrl+D or uses command exit
void free_memory(char* args[],char* search_path[],char* absolute_path[],char* output_redirected_files[],char* temp[])
{
    int i;
    for(i=0;i<ARRAY_SIZE;i++)
        free(args[i]);
    for(i=0;i<PATH_SIZE;i++)
    {
        free(search_path[i]);
        free(absolute_path[i]);
        free(output_redirected_files[i]);
    }
    for(i=0;i<3;i++)
        free(temp[i]);
}

//generic error message on every error. Continue after error
void print_error()
{
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

//user defined exit command which is called when user types exit in the wish shell
//wish> exit
int udf_exit()
{
    exit(0);
}

//user defined cd command 
//wish> cd directory-path
int udf_cd(char* args[],int count)
{
    int retcode=-1;
    if(count != 2)
        return retcode;
    retcode = chdir(args[1]);
    return retcode;
}

//user defined path command which sets the commands in the paths specified by the user
//path path1 [path2 ...]
//function returns current #paths to traverse in path_array
int udf_path(char* path_array[],int count, char* args[])
{
    int i;
    int cnt=0;
    for(i=1;i<count;i++)
    {
        strcpy(path_array[cnt++],args[i]);
    }
    path_array[cnt]=NULL;
    return cnt;
}

//convert the user string into char* array args.
//found[0]!=0 deals with extranious spaces, tabs
//we need a NULL terminated array for execv
//handling the case when the redirection(>) operator and parallel command(&) operator are without spaces
int tokenize_user_command(char* buffer, char* args[])
{
    int count=0,start=0,len=0,i=0;
    char* found;

    while((found = strsep(&buffer," \t")) != NULL)
    {
        if(found[0] == 0)
            continue;
        len=strlen(found);
        i=0;
        start=0;
        while(i<len)
        {
            if((found[i] == '>') || found[i] == '&')
            {
                if(i!=start)
                    strncpy(args[count++],&found[start],i-start);
                strncpy(args[count++],&found[i],1);
                start=i+1;
            }
            i++;
        }
        
        if(start!=len)
            strncpy(args[count++],&found[start],len-start);
    }
    args[count]=NULL;
    return count;
}

//get the absolute path from the args[0] command name.
//to get the absolute path, we iterate through all the user provided paths in the path command
char* get_absolute_path(char* search_path[],int search_array_len,char* cmd, char* absolute_path)
{
    int i;
    if(search_path[0] == NULL)
        return NULL;
    for(i=0;i<search_array_len;i++)
    {
        memset(absolute_path,0,50);
        strcat(absolute_path,search_path[i]);
        strcat(absolute_path,"/");
        strcat(absolute_path,cmd);
        if(!access(absolute_path,X_OK))
            return absolute_path;
    }
    return NULL;
}

//validate if the redirection operator is not before the command name
//and we dont have more than 1 files specified after the redirection operation. Error out if so
int validate_redirection(char* subargs[],char* filename)
{
    int i=0;
    int redirection_idx=-1,count=0;
    while(subargs[i]!=NULL)
    {
        if(!strcmp(subargs[i],">"))
        {
            redirection_idx=i;
            subargs[i]=NULL;
            count++;
        }
        i++;
    }
    //we have a single redirection operator and a single filename post that
    //also, redirection operator should be followed by a command
    if(count == 1 && subargs[redirection_idx+2] == NULL && redirection_idx != 0)
        strcpy(filename,subargs[redirection_idx+1]);
    else if(redirection_idx == -1)
        strcpy(filename,"stdout");
    else
        //error
        return 0;
    return 1;
}

//Populate the parallel_indices array which stores the indices of the start of each parallel command
//e.g. command = ps fx & cat hello.txt > out1 & wc -l data.txt
//               0  1  2  3   4       5   6   7 8   9   10   
//parallel_indices={0,3,8}
int validate_parallel_command(char* args[],int count,int parallel_indices[])
{
    int i,prll_idx=0;
    parallel_indices[prll_idx++]=0;

    //fill in the parallel indices array
    for(i=0;i<count;i++)
    {
        if(!strcmp(args[i],"&"))
        {
            if(i < count-1 && i != 0 )
                parallel_indices[prll_idx++]=i+1;
            args[i]=NULL;
        }
    }
    return prll_idx;
}

int main(int argc, char* argv[])
{
    //used for getline
    char* buffer=NULL;
    size_t buflen=0;
    ssize_t characters;

    //storing the contents of the user input in the args array. It tokenizes each command entered by the user except the whitespaces, tabs
    int i;
    char* args[ARRAY_SIZE];
    for(i=0;i<ARRAY_SIZE;i++)
    {
        args[i]=(char*)malloc(sizeof(char)*ELEMENTS);
        memset(args[i],0,sizeof(char)*ELEMENTS);
    }
    int count=0;

    //shell name
    char* filename=argv[0]+2;

    int retcode=0;

    //filepointer for the BATCH file
    FILE* fp=NULL;
    if(argc == 2)
    {
        fp=fopen(argv[1],"r");
        if(!fp)
        {
            print_error();
            exit(1);
        }
    }

    if (argc > 2)
    {
        print_error();
        exit(1);
    }

    //the absolute path after prepending the PATH variable for the one or more commands ( more in parallel case)
    char* absolute_path[PATH_SIZE];
    for(i=0;i<PATH_SIZE;i++)
    {
        absolute_path[i]=(char*)malloc(sizeof(char)*ELEMENTS);
        memset(absolute_path[i],0,sizeof(char)*ELEMENTS);
    }

    //it contains the list of paths specified by the user in path command
    char* search_path[PATH_SIZE];
    for(i=0;i<PATH_SIZE;i++)
    {
        search_path[i]=(char*)malloc(sizeof(char)*ELEMENTS);
        memset(search_path[i],0,sizeof(char)*ELEMENTS);
    }

    //this tracks the current number of paths to iterate over based on user input
    int search_array_len=0;

    //setting initial search path to /bin
    char* temp[3];
    for(i=0;i<3;i++)
    {
        temp[i]=(char*)malloc(sizeof(char)*ELEMENTS);
        memset(temp[i],0,sizeof(char)*ELEMENTS);
    }
    strcpy(temp[0],"path");
    strcpy(temp[1],"/bin");
    temp[2]=NULL;
    search_array_len = udf_path(search_path,2,temp);

    //parallel commands tracker: parallel_indices array stores the indices of start of each parallel command
    //prll_idx returns the count of the current elements of the array
    int parallel_indices[PATH_SIZE]={-1};
    int prll_idx;

    //redirection command tracker: output_redirected_files array stores the filenames, if redirection is present: else "stdout"
    char* output_redirected_files[PATH_SIZE];
    for(i=0;i<PATH_SIZE;i++)
    {
        output_redirected_files[i]=(char*)malloc(sizeof(char)*ELEMENTS);
        memset(output_redirected_files[i],0,sizeof(char)*ELEMENTS);
    }

    while(1)
    {
        if(argc == 1)     //interative mode
        {
            printf("%s> ",filename);
            characters=getline(&buffer,&buflen,stdin);
        }
        else    //batch mode, dont need the wish> prompt
            characters=getline(&buffer,&buflen,fp);

        //remove the trailing newline from the getline command
        if(characters != -1)
            buffer[strcspn(buffer,"\n")] = 0;
        else    //the case of Ctrl + D (EOI) entered by the user
        {
            free_memory(args,search_path,absolute_path,output_redirected_files,temp);
            exit(0);
        }

        //this re-initializes the args array for every new command entered by the user (for every iteration of while(1))
        //also, reallocates space for the indices which were set to NULL in the previous iteration
        for(i=0;i<count;i++)
        {
            if(!args[i])
                args[i]=(char*)malloc(sizeof(char)*ELEMENTS);
            memset(args[i],0,ELEMENTS);
        }
        args[count]=(char*)malloc(sizeof(char)*ELEMENTS);
        memset(args[count],0,ELEMENTS);

        //read the user input from buffer and populate the args array
        count = tokenize_user_command(buffer,args);

        //If the user enters only spaces or tabs and no command, then continue to prompt for input
        if(!args[0])
            continue;

        //args[0] stores the command entered by the user.
        if(!strcmp(args[0],"exit"))
        {
            //validate that exit has no more parameters passed and free the heap and exit gracefully
            if(count == 1)
            {
                free_memory(args,search_path,absolute_path,output_redirected_files,temp);
                udf_exit();
            }
            print_error();
            continue;
        }
        else if(!strcmp(args[0],"cd"))
        {
            retcode = udf_cd(args,count);
            if(retcode == -1)
            {
                print_error();
            }
            continue;
        }
        else if(!strcmp(args[0],"path"))
        {
            //reinitialize the search_path array for the new run
            for(i=0;i<PATH_SIZE;i++)
            {
                if(!search_path[i])
                    search_path[i]=(char*)malloc(sizeof(char)*ELEMENTS);
                memset(search_path[i],0,sizeof(char)*ELEMENTS);
            }
            search_array_len = udf_path(search_path,count,args);
            continue;
        }

        prll_idx=validate_parallel_command(args,count,parallel_indices);
        if(prll_idx == -1)
        {
            print_error();
            continue;
        }

        //reinitializing the absolute_path array and output_redirected_files array for the current run
        for(i=0;i<PATH_SIZE;i++)
        {
            if(!absolute_path[i])
                absolute_path[i]=(char*)malloc(sizeof(char)*ELEMENTS);
            memset(absolute_path[i],0,ELEMENTS);
            memset(output_redirected_files[i],0,ELEMENTS);
        }

        //here onwards, we loop for all the parallel commands based on the indices in parallel_indices array
        for(i=0;i<prll_idx;i++)
        {
            if(!args[parallel_indices[i]])
                continue;
            retcode = validate_redirection(&args[parallel_indices[i]],output_redirected_files[i]);
            if(!retcode)
            {
                print_error();
                continue;
            }

            //get full path of the command to pass to execv syscall
            absolute_path[i] = get_absolute_path(search_path,search_array_len,args[parallel_indices[i]],absolute_path[i]);
            if(!absolute_path[i])
            {
                print_error();
                continue;
            }

            int pid=fork();
            if(pid<0)   //fork fails
            {
                print_error();
                continue;
            }
            if(pid==0)  //in the child process
            {
                if(strcmp(output_redirected_files[i],"stdout"))     //check if the file is to be redirected or not?
                {
                    int outfile=open(output_redirected_files[i],O_WRONLY | O_TRUNC | O_CREAT,0644);
                    if(outfile == -1)
                    {
                        print_error();
                        continue;
                        //exit(1);
                    }

                    //instead of using stdout and stderr, use the file specified by the user
                    retcode=dup2(outfile,1);
                    if(retcode == -1)
                    {
                        print_error();
                        continue;
                    }
                    retcode=dup2(outfile,2);
                    if(retcode == -1)
                    {
                        print_error();
                        continue;
                    }
                }

                retcode=execv(absolute_path[i],&args[parallel_indices[i]]);
                if(retcode == -1)
                    print_error();
            }
       }

        //this is the parent process, waiting for all the children to finish their execution
        for(i=0;i<prll_idx;i++)
            wait(NULL);
    }
    return 0;
}

