/*
 * Copyright (C) 2019, Xilinx Inc - All rights reserved
 * Xilinx Worker-Launcher Application
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include </opt/xilinx/xrm/include/xrm.h>
#include <time.h>

#include <cctype>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>

#include "parser.h"

#define MAX_ERRORS 4096
#define CMD_SIZE   512


typedef struct CleanupInfo
{
    int pid;
    char* filename;
    pthread_mutex_t* mutex;
    pthread_cond_t* cond;
    int* running_procs;
    char** failed_filenames;
    int* failed_sources;
    int* succeeded_sources;
} CleanupInfo;

long long get_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000000 + tv.tv_usec) + 42 * 60 * 60 * INT64_C(1000000);
}

// wait for a process to finish, then update the status of running/succeeded/failed sources
static void* cleanup_pid (void* arg)
{
    CleanupInfo* info = (CleanupInfo*)arg;

    // wait for the process to end
    int wstatus = 0;
    waitpid (info->pid, &wstatus, 0);

    pthread_mutex_lock (info->mutex);

    if ((WIFEXITED(wstatus)) && (WEXITSTATUS(wstatus) == 0))
    {
        // if the process succeeded, free the filename
        delete info->filename;
        *info->succeeded_sources += 1;
    }
    else
    {
        // if the process failed, add the source file name to the list of failed sources
        if (*info->failed_sources < MAX_ERRORS)
            info->failed_filenames[*info->failed_sources] = info->filename;
        else
            delete info->filename;
        *info->failed_sources += 1;
    }

    // decrement the number of running processes
    *info->running_procs -= 1;
    pthread_cond_signal (info->cond);
    pthread_mutex_unlock (info->mutex);
    delete info;

    return NULL;
}

// separate a command line string into an array of arguments
int separate_cmdline (char* cmdline, char* cmd_params[CMD_SIZE])
{
    int index = 0; // index of a command line we are reading from
    int param = 0; // current parameter
    int param_index = 0; // current character in the parameter
    int in_single_quote = 0; // are we in a single quote
    int in_double_quote = 0; // are we in a double quote
    while (cmdline[index] != '\0')
    {
        // skip spaces
        while ((cmdline[index] != '\0') && (isspace(cmdline[index])))
            index++;

        // loop copying the parameter
        cmd_params[param] = &cmdline[param_index];
        while ((cmdline[index] != '\0') && ((!isspace(cmdline[index])) || in_single_quote || in_double_quote))
        {
            if (cmdline[index] == '\'')
                in_single_quote = !in_single_quote;
            if (cmdline[index] == '\"')
                in_double_quote = !in_double_quote;
            if ((cmdline[index] != '\'') && (cmdline[index] != '\"'))
            {
                cmdline[param_index] = cmdline[index];
                ++param_index;
            }
            ++index;
        }
        if (in_single_quote || in_double_quote)
        {
            // error if we reached the end of the command line and are still in a quote
            fprintf (stderr, "command line error!\n");
            return -1;
        }
        cmdline[param_index] = '\0';
        ++param_index;
        ++param;
        ++index;
    }
    cmd_params[param] = NULL;
    return 0;
}

static int insert_out_file_with_ch(char *ch_str, char *run_cmd, char *org_out_file_name) {
    char outName[1080];
    char orgOutName[1077];
    char* extn = strstr(org_out_file_name,".");
    if (extn!=NULL) {
        strncpy(orgOutName,org_out_file_name,extn-org_out_file_name);
        sprintf(outName," %s_%s%s ",orgOutName,ch_str,extn);
        strcat(run_cmd, outName);
    } else {
        strcat(run_cmd," ");
        strcat(run_cmd, org_out_file_name);
    }
    return 0;
}

static int first_occurance_idx(const char* src_string, const char* match_word)
{
    int slen = strlen(src_string);

    for (int i=0; i<slen; i++)
    {
        if(strstr(&src_string[i],match_word) == &src_string[i])			
        {
           return i;			
        }
    }	
    return -1;
}

static char* replace_scl_hwdev(const char* str, xrm_dev_list* pop_dev_list)
{
    char orig[256] = {};
    char mod[256] = {};
    char pre_mod[1024] = {};
    char post_mod[1024] = {};
    char *result = (char*)calloc(1, 4096);
    char *endptr;
	
    int i = 0, olen= 0, vt_id = 0, hw_id= 0;   
    errno =0;	

    i = first_occurance_idx(str,"lxlnx_hwdev");
    if (i>= 0)
    {
       strncpy(pre_mod,&str[0],i);
       strcpy(post_mod,&str[i]);
    }

    i= first_occurance_idx(post_mod,":");
    if (i>= 0)
    {	
       strncpy(orig,post_mod,i);
       olen = strlen(orig);

       if (strstr(orig,"lxlnx_hwdev="))
       {
          strcpy(mod,&orig[strlen("lxlnx_hwdev=")]);
          vt_id = (int) strtol(mod, &endptr, 0);

          //check for strtol errors
          if (errno != 0)
          {
              perror("strtol");
              exit(0);
          }

          if (vt_id > -1)
             hw_id = pop_dev_list->hw_dev_ids[vt_id];
         
          sprintf(result,"%slxlnx_hwdev=%d",pre_mod,hw_id);
          strcat(result,&post_mod[olen]);
       }
    }
    return result;
}

int prepare_ffmpeg_run_cmd(char* source_filename, char *ch_str, char *run_cmd, xrm_dev_list* pop_dev_list) {

    char *word = strtok (NULL, " ");
    int i=0, tmp=0;	
    char s_dev[128];
    char * pch, *endptr;

    memset (s_dev,0,sizeof(s_dev));


    while (word != NULL)
    {
        errno = 0;
        pch = strstr(word,"lxlnx_hwdev=");
        if (pch==NULL)
        {	  
            strcat(run_cmd," ");
            strcat(run_cmd, word);
        }  

        if (strcmp(word,"-y")==0)
        {
          word = strtok (NULL, " ");
          if (word != NULL) {
              insert_out_file_with_ch(ch_str, run_cmd, word);
          }
       }
       else
       {
          if (strcmp(word, "-i")==0)
          {
              strcat(run_cmd, " ");
              strcat(run_cmd, source_filename);		  
          }
          else if(strcmp(word,"-lxlnx_hwdev") == 0)
          {
              word = strtok (NULL, " ");
              tmp = (int) strtol(word, &endptr, 0);

              //check for strtol errors
              if (errno != 0)
              {
                 perror("strtol");
                 return -1;
              }

              if (tmp < 0)  return -1;

              strcat(run_cmd, " ");
              sprintf(s_dev," %d", pop_dev_list->hw_dev_ids[tmp]);
              strcat(run_cmd, s_dev);		  
          }
          if (pch != NULL)
          {
              char* str1;
              str1=replace_scl_hwdev(word,pop_dev_list);
              strcat(run_cmd," ");
              strcat(run_cmd, str1);
              free(str1);
          }		  
       }  
       word = strtok (NULL, " ");
    }
    return 0;
}

int prepare_mpsoc_app_run_cmd(char* source_filename, char *ch_str, char *run_cmd) {
    char *ch_params = NULL;
    char *out_file = NULL;    
    char *word = strtok (NULL, " ");

    if (word == NULL) {
        return -1;
    }
    strcat(run_cmd," ");
    strcat(run_cmd, word);

    //Insert source file
    strcat(run_cmd, " ");
    strcat(run_cmd, source_filename);

    // copy LA depth, spatial_aq_mode, temporal_aq_mode, thumbnail path,
    for (int i= 0; i < 4; i++) {
        word = strtok (NULL, " ");
        if (word == NULL) {
            return -1;
        }
        strcat(run_cmd," ");
        strcat(run_cmd, word);
    }

    //<output parameter file> <h264 or hevc output video file>
    ch_params = strtok (NULL, " ");
    out_file = strtok (NULL, " ");
    while (ch_params && out_file) {
        strcat(run_cmd," ");
        strcat(run_cmd, ch_params);
        insert_out_file_with_ch(ch_str, run_cmd, out_file);
        ch_params = strtok (NULL, " ");
        out_file = strtok (NULL, " ");
    }
    return 0;
}

int prepare_gst_run_cmd(char* source_filename, char *ch_str, char *run_cmd, xrm_dev_list* pop_dev_list) {
    int sink_count = 0;
    int tmp=0;
    char s_dev[128];

    char *word = strtok (NULL, " ");
    while (word != NULL)
    {
        char *fileout;
        strcat(run_cmd," ");
        strcat(run_cmd, word);

        if (strstr(word,"filesink"))
        {
            word = strtok (NULL, " ");
            strcat(run_cmd," ");

            if (strncmp(word,"location=", 9)==0) {
                char *tmp = word+8;
                strcat(run_cmd,"location=");
                /* skipping white spaces & equal symbol */
                while (*tmp == ' ' || *tmp == '=') {
                    tmp++;
                    continue;
                }
                insert_out_file_with_ch (ch_str, run_cmd, tmp);
            }
        }
        else
        {
            if (strcmp(word, "filesrc")==0)
            {
                strcat(run_cmd, " ");
                strcat(run_cmd, "location=");
                strcat(run_cmd, source_filename);
                strcat(run_cmd, " ");
	    }
            else if (strcmp(word, "fpsdisplaysink") == 0)
            {
                char sink_name[1024];
                sprintf (sink_name, "fpsdisplaysink_%s_%d",ch_str, sink_count);
                strcat(run_cmd, " ");
                strcat(run_cmd, "name=");
                strcat(run_cmd, sink_name);
                sink_count++;
                while(1) {
                    char *ptr;
                    ptr = strtok(NULL, " ");
                    if (!ptr) break;
                    strcat(run_cmd, " ");
                    if (strstr(ptr, "name=")) {
                        sprintf(sink_name, "%s_%s", ptr, ch_str);
                        strcat(run_cmd, sink_name);
                    } else {
                      strcat(run_cmd, ptr);
                    }
                    if (strstr(ptr, "!")) {
                       break;
                    }
                }
            }
	    else if(strncmp(word,"dev-idx=", 8) == 0)
            {
	      sscanf(word, "dev-idx=%d",&tmp);

              sprintf(s_dev,"%d", pop_dev_list->hw_dev_ids[tmp]);
	      *(run_cmd + (strlen(run_cmd) - 1)) = '\0';
              strcat(run_cmd, s_dev);
            }
        }
        word = strtok (NULL, " ");
    }
    return 0;
}

// allocate resources, launch a process, wait for it to end, then release the allocated resources
int launch (int pipefd, char* source_filename, xrmCuPoolPropertyV2* xrm_transcode_cu_pool_prop, xrm_dev_list* pop_dev_list, char* pre_src_cmdline, int log_en)
{
    xrmCuListResInforV2* cuListResInfor;
    xrmCuPoolResInforV2* xrm_transcode_cu_pool_res;
    xrmCuResInforV2* cuResInfor;
	
    // initialize
    xrmContext *ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
    if (ctx == NULL)
    {
       fprintf (stderr, "Create context failed\n");
       return -1;
    }
    xrm_transcode_cu_pool_res = (xrmCuPoolResInforV2*)malloc(sizeof(xrmCuPoolResInforV2));
    memset(xrm_transcode_cu_pool_res, 0, sizeof(xrmCuPoolResInforV2));
	
    // allocate resources
    uint64_t reservation_id = xrmCuPoolReserveV2(ctx, xrm_transcode_cu_pool_prop, xrm_transcode_cu_pool_res);
    if (reservation_id == 0)
    {
        fprintf (stderr, "xrmCuPoolReserve: fail to reserve transcode cu pool :%lu\n",reservation_id);
        if (ctx)
            xrmDestroyContext(ctx);
	return -1;
    }
	
    for (int i=0; i<pop_dev_list->num_devs; i++)
    {
        cuListResInfor = &(xrm_transcode_cu_pool_res->cuListResInfor[0]);
        cuResInfor = &( cuListResInfor->cuResInfor[pop_dev_list->dev_start_cuidx[pop_dev_list->vt_dev_ids[i]]]);
		
        pop_dev_list->hw_dev_ids[pop_dev_list->vt_dev_ids[i]] = cuResInfor->deviceId;
    }
		
    // send signal to parent process that allocation is complete
    char buf = '\0';
    (void) !write (pipefd, &buf, 1);
    close (pipefd);

    // set environmant variable with service id
    char alloc_reservation_id[16];
    sprintf (alloc_reservation_id, "%lu", reservation_id);

    setenv ("XRM_RESERVE_ID", alloc_reservation_id, 1);
    if (log_en == 1)
       fprintf (stderr, "xrm_reservation_id =%lu \n",reservation_id);

    // write console output to seperate log files with '-enable-logging' option 
    int file_out = -1; 
    char fname[256]; 
    struct stat dstat; 
    int ret = -1; 
    if (log_en == 1) 
    { 
       if (stat("/var/tmp/xilinx", &dstat) == -1) 
       { 
          ret = mkdir("/var/tmp/xilinx",0777); 
          if (ret != 0) 
          { 
            fprintf (stderr, "Couldn't create /var/tmp/xilinx folder to write logs. Err = %s", strerror(errno));
            return EXIT_FAILURE; 
          }         
       } 
       sprintf(fname,"/var/tmp/xilinx/job_%lu_res.log",reservation_id); 
       file_out =  open(fname,O_RDWR | O_CREAT, 0644); 
    } 

    
    // fork new process
    int pid = fork ();
    if (pid < 0)
    {
        perror("worker.cpp::launch() Fork failed\n") ;
        if (ctx && reservation_id)
	{
	    if(xrmCuPoolRelinquish (ctx, reservation_id))
     	    {
                if (log_en == 1)
                    fprintf (stderr, "xrmCuPoolRelinquish = %lu\n",reservation_id);
        	if(xrmDestroyContext(ctx) != XRM_SUCCESS)
          	    fprintf (stderr, "XRM destroy context failed! %lu\n",reservation_id);
            }
	}
	else
	{
            if(!ctx)
		fprintf (stderr, "Null XRM context. xrmCuPoolRelinquish & xrmDestroyContext failed!\n");
	    else		
		fprintf (stderr, "Null reservation id. xrmCuPoolRelinquish failed!\n");
	}
        return -1;
    }
    if (pid == 0) 
    {
    // insert source file into command lineand add ReservationID to the output for each process
    char cpy_pre_out_cmdline[4096];
    strcpy (cpy_pre_out_cmdline, pre_src_cmdline);

    char cmdline_test[4096];
    memset (cmdline_test, 0, sizeof (cmdline_test));

    char*word;
    char c_id[256];

    sprintf(c_id,"%lu",reservation_id);

    word =strtok(cpy_pre_out_cmdline, " ");
    strcpy(cmdline_test,word);

    if (strstr(word, "ffmpeg") != NULL) {
        prepare_ffmpeg_run_cmd(source_filename, c_id, cmdline_test, pop_dev_list);
    } else if (strstr(word, "mpsoc_app") != NULL) {
        prepare_mpsoc_app_run_cmd(source_filename, c_id, cmdline_test);
    } else if (strstr(word, "gst-launch-1.0") != NULL) {
        prepare_gst_run_cmd(source_filename, c_id, cmdline_test, pop_dev_list);
    } else {
        fprintf (stderr, "Error : Unknown Executable %s\n", word);
        return -1;
    }

     // separate command line into array of arguments
     /* Need to dynamically calculate size of parameters */
     char* cmd_params[CMD_SIZE];
     if (separate_cmdline (cmdline_test, cmd_params))
         return -1;
    char cmd[CMD_SIZE];
    strcpy (cmd, cmd_params[0]);
    const char* file = strrchr (cmd, '/');
    if (file)
          file++;
    else
          file = cmd;
    strcpy (cmd_params[0], file);

    if (log_en == 1) 
    { 
       if (dup2(file_out, 0) < 0)
          fprintf (stderr, "Unable to copy log file descriptor to stdin\n"); 
       if (dup2(file_out, 1) < 0)
          fprintf (stderr, "Unable to copy log file descriptor to stdout\n"); 
       if (dup2(file_out, 2) < 0)
          fprintf (stderr, "Unable to copy log file descriptor to stderr\n");  
 
       close (file_out); 
    } 

    // execute command
    if (execvp (cmd, cmd_params) != 0)
    {
        fprintf (stderr, "Error when trying to execvp() : (%s)\nffmpeg failed\n", strerror(errno));
        //exit(1);//if want to exit when there is a error
    }

    return 0;
    }

    // wait for child to finish
    int wstatus;
    waitpid (pid, &wstatus, 0);
    // release resources
    if(xrmCuPoolRelinquishV2 (ctx, reservation_id))
    {
       if (log_en == 1)
          fprintf (stderr, "------------xrmCuPoolRelinquish =%lu\n",reservation_id);

       if(xrmDestroyContext(ctx) != XRM_SUCCESS)
         fprintf (stderr, "XRM destroy context failed! %lu\n",reservation_id);
    }
    
    if (WIFEXITED(wstatus))
        return WEXITSTATUS(wstatus);
    else
        return -1;
}

int main (int argc, char *argv[])
{
    struct termios terminfo;
    bool got_term_attr;
    int sys_ret, ret=-1;
    int log_en = 0;
    const char *opt_log = "-enable-logging";
    xrm_dev_list pop_dev_list;

    memset(&pop_dev_list, 0, sizeof(pop_dev_list));
	
    if (argc < 3)
    {
        fprintf (stderr, "Usage:\n");
        fprintf (stderr, "      %s <source files file name> <cmdline  file name> \n\n", argv[0]);
        fprintf (stderr, "Usage with logging:\n");
        fprintf (stderr, "      %s <source files file name> <cmdline  file name> %s \n\n", argv[0], opt_log);
        return -1;
    }
    
    if (argc == 4)
    {
       if (strncmp (opt_log, argv[3], strlen(opt_log)) == 0)  
          log_en = 1;
    }

    got_term_attr = true;
    if (tcgetattr(STDOUT_FILENO, &terminfo) != 0) {
        perror("tcgetattr() error");
	got_term_attr = false;
    }

    // initialize
    xrmContext *ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
    if (ctx == NULL)
    {
       fprintf (stderr, "Create context failed\n");
       return -1;
    }

    xrmCuPoolPropertyV2 xrm_transcode_cu_pool_prop;
    memset(&xrm_transcode_cu_pool_prop, 0, sizeof(xrm_transcode_cu_pool_prop));

    // get properties and command line from parameter file
    int param_file = open (argv[2], O_RDONLY);
    char pre_src_cmdline[4096];

    if (fill_props (param_file, &xrm_transcode_cu_pool_prop, pre_src_cmdline, &pop_dev_list) != 0)
    {
        fprintf (stderr, "Failed to run given %s run command.\n", argv[2]);
        return -1;
    }

   /*for (int k=0; k<pop_dev_list.num_devs; k++)
   {
       printf("id=%d vt_dev=%d\n",k,pop_dev_list.vt_dev_ids[k]);
   }*/
	
    close (param_file);

    int sources_file = open (argv[1], O_RDONLY);

    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_init (&mutex, NULL);
    pthread_cond_init (&cond, NULL);
    int running_procs = 0;
    int wait = 0;

    char** failed_filenames = (char**)calloc (MAX_ERRORS, sizeof(char*));
    int failed_sources = 0;
    int succeeded_sources = 0;

    long long start_time = get_time();

    int done = 0;
    int worker = 1;
    ret = -1;
    fprintf (stderr, "\n===================================================================\n");
    fprintf (stderr, "Launching processes started :");
    if (log_en == 1)
       fprintf (stderr, "\nLog files per process are written to /var/tmp/xilinx using reservation id.");
    fprintf (stderr, "\n===================================================================\n");

    do
    {
        // get next source file
        char filename[1024];
        switch (get_line (sources_file, filename))
        {
            case PARSER_RET_EOF:
                done = 1;
                ret = 0;
                break;
            case PARSER_RET_ERROR:
                fprintf (stderr, "Reading source files failed!\n");
                done = 1;
                ret = -1;
                break;
            case PARSER_RET_EOL:
                break;
            case PARSER_RET_SUCCESS:
            {
                if (strlen(filename) == 0)
                    break;

                printf("\nNumber of slots= %d\n",xrmCheckCuPoolAvailableNumV2(ctx, &xrm_transcode_cu_pool_prop));

                // wait until resources are available
                while (xrmCheckCuPoolAvailableNumV2(ctx, &xrm_transcode_cu_pool_prop) == 0)
                {
                    if (wait == 0)
                    {
                       wait = 1;
                       fprintf (stderr, "\n===================================================================\n");
                       fprintf (stderr, "Launched max parallel process for the given job description. \nLeftover streams have to wait for current ones to finish.\n"); 
                       fprintf (stderr, "===================================================================\n\n");
                    }
                    
                    sleep(1);
                }

                sleep(1);

                // create pipe so child can notify parent when resources are allocated
                int pipefd[2];
                if (pipe(pipefd) == -1)
                {
                    fprintf (stderr, "Pipe creation failed\n");
                    done = 1;
                    ret = -1;
                    break;
                }

                int pid = fork ();

                if (pid < 0)
                {
                    perror("worker.cpp:: Fork failed\n") ;
                    done = 1;
                    ret = -1;
                    break;
                }


                if (pid == 0)
                {
                    // run child
                    close (pipefd[0]);

                    ret = launch (pipefd[1], filename, &xrm_transcode_cu_pool_prop, &pop_dev_list, pre_src_cmdline, log_en);

                    done = 1;
                    worker = 0;
                    break;
                }

                // wait for child to allocate resources
                close (pipefd[1]);
                char buf;
                (void) !read(pipefd[0], &buf, 1);
                close (pipefd[0]);

                // create thread to clean up after child
                CleanupInfo* info = (CleanupInfo*)malloc (sizeof(CleanupInfo));
                info->pid = pid;
                info->filename = (char*) malloc (strlen(filename) + 1);
                strcpy (info->filename, filename);
                info->mutex = &mutex;
                info->cond = &cond;
                info->running_procs = &running_procs;
                info->failed_filenames = failed_filenames;
                info->failed_sources = &failed_sources;
                info->succeeded_sources = &succeeded_sources;
                pthread_mutex_lock (&mutex);
                running_procs++;
                pthread_mutex_unlock (&mutex);
                pthread_t threadh;
                pthread_attr_t attr;
                pthread_attr_init (&attr);
                pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
                ret = pthread_create (&threadh, &attr, cleanup_pid, (void*)info);
                if (ret != 0) {
                  perror("worker.cpp:: pthread_create() failed\n") ;
                  break;
                }
                break;
            }
        }
    }while(!done);

    if (worker)
    {
        // wait for all children to complete
        pthread_mutex_lock (&mutex);
        while (running_procs > 0)
            pthread_cond_wait (&cond, &mutex);
		
        xrmDestroyContext(ctx);
        pthread_mutex_unlock (&mutex);

        long long end_time = get_time();

        // print stats
        fprintf (stderr, "\n===================================================================\n");
        if (failed_sources == 0)        
            fprintf (stderr, "All %d source(s) succeeded!\n", succeeded_sources);        
        else
        {
            for (int i = 0; i < ((failed_sources < MAX_ERRORS) ? failed_sources : MAX_ERRORS); i++)
            {
                fprintf (stderr, "%s failed!\n", failed_filenames[i]);
                delete (failed_filenames[i]);
            }
            fprintf (stderr, "%d source(s) succeeded.\n", succeeded_sources);
            fprintf (stderr, "%d source(s) failed!\n", failed_sources);
        }

        fprintf (stderr, "===================================================================\n");

        long long time = (end_time - start_time) / 1000;
        long long min = time / 60000;
        fprintf (stderr, "Time taken: %lld min %.3f sec\n\n", min, (time - (min * 60000.0)) / 1000.0);
    }

    // clean up
    pthread_mutex_destroy (&mutex);
    pthread_cond_destroy (&cond);

    close (sources_file);

//    xrm_destroy_context (ctx);
    if (got_term_attr) {
        if (tcsetattr(STDOUT_FILENO, TCSADRAIN, &terminfo) != 0) {
            /* fallback to reset command. TODO:currently return value not handled  */
            sys_ret = system("reset");
        }
    } else {
        /* fallback to reset command. TODO:currently return value not handled  */
        sys_ret = system("reset");
    }

    delete failed_filenames;

    return ret;
}
