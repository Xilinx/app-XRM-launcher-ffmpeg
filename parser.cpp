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
#include <stdlib.h>
#include "parser.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <dlfcn.h>
#include <xrm_limits.h>
#define MAX_CH_SIZE 16284 //XRM_MAX_PLUGIN_FUNC_PARAM_LEN
#define XRM_PRECISION_1000000_BIT_MASK(load) ((load << 8))

namespace pt=boost::property_tree;

PARSER_RET get_value (int file, char* value)
{
	int index = 0;
	PARSER_RET ret = PARSER_RET_SUCCESS;
	char letter = '\0';
	int bytesread = 0;
	do
	{
		bytesread = read (file, &letter, 1);
		if (bytesread == 0)
		{
			ret = PARSER_RET_EOF;
			goto done;
		} else if (bytesread < 0)
		{
			ret = PARSER_RET_ERROR;
			goto done;
		}
	} while ((letter == ' ') || (letter == '\t'));

	if ((letter == '\n') || (letter == '\r'))
	{
		ret = PARSER_RET_EOL;
		goto done;
	}
	
	do
	{
		value[index++] = letter;
		bytesread = read (file, &letter, 1);
		if (bytesread == 0)
		{
			ret = PARSER_RET_EOF;
			goto done;
		} else if (bytesread < 0)
		{
			ret = PARSER_RET_ERROR;
			goto done;
		}
	} while ((letter != ' ') && (letter != '\t') && (letter != '\n') && (letter != '\r'));
	if ((letter == '\n') || (letter == '\r'))
		ret = PARSER_RET_EOL;
done:
	value[index] = '\0';
	return ret;
}

PARSER_RET get_line (int file, char* value)
{
	int index = 0;
	PARSER_RET ret = PARSER_RET_SUCCESS;
	char letter = '\0';
	int bytesread = 0;
	do
	{
		bytesread = read (file, &letter, 1);
		if (bytesread == 0)
		{
			ret = PARSER_RET_EOF;
			goto done;
		} else if (bytesread < 0)
		{
			ret = PARSER_RET_ERROR;
			goto done;
		}
	} while ((letter == ' ') || (letter == '\t'));
	
	if ((letter == '\r') || (letter == '\n'))
	{
		ret = PARSER_RET_EOL;
		goto done;
	}

	do
	{
		value[index++] = letter;
		bytesread = read (file, &letter, 1);
		if (bytesread == 0)
		{
			ret = PARSER_RET_EOF;
			goto done;
		} else if (bytesread < 0)
		{
			ret = PARSER_RET_ERROR;
			goto done;
		}
	} while ((letter != '\n') && (letter != '\r'));
	if (value[0] == '#')
		ret = PARSER_RET_EOL;
done:
	value[index] = '\0';
	return ret;
}

PARSER_RET get_key_value_pair (int file, char* key, char* value)
{
	PARSER_RET r1 = get_value (file, key);
	if (key[0] == '#')
	{
		if (r1 == PARSER_RET_ERROR)
			return PARSER_RET_ERROR;
		if (r1 == PARSER_RET_SUCCESS)
		{
			PARSER_RET r2 = get_line (file, value);
			if (r2 == PARSER_RET_ERROR)
				return PARSER_RET_ERROR;
		}
		return PARSER_RET_EOL;
	}
	if ((r1 == PARSER_RET_EOL) && (strlen(key) != 0))
		return PARSER_RET_ERROR;
	if (r1 != PARSER_RET_SUCCESS)
		return r1;
	
	char p2[128];
	PARSER_RET r2 = get_value (file, p2);
	if (r2 != PARSER_RET_SUCCESS)
		return PARSER_RET_ERROR;
	if (strncmp(p2, "=", 2) != 0)
		return PARSER_RET_ERROR;

	PARSER_RET r3 = get_line (file, value);
	if (r3 == PARSER_RET_ERROR)
		return r3;
	
	return PARSER_RET_SUCCESS;
}

int get_ffmpeg_cmd_vt_devs(char *run_cmd, xrm_dev_list* pop_dev_list) 
{
    long dev_id = 0;
    bool dev_list[MAX_XLNX_DEVS];
    memset(dev_list, false, MAX_XLNX_DEVS*sizeof(bool));	

    char *word = strtok (run_cmd, " ");
    int i=0;
    char *pch, *endptr;
	
    pop_dev_list->num_devs = 1;
			
    while (word != NULL)
    {   
        if((strcmp(word,"-lxlnx_hwdev") == 0) || (strcmp(word,"lxlnx_hwdev=") == 0) || (strncmp(word, "dev-idx=", 8)) == 0)
        {
            errno = 0;
	    if (strncmp(word, "dev-idx=", 8) == 0)
	    {
		sscanf(word, "dev-idx=%ld", &dev_id);
	    }
	    else
	    {
                word = strtok (NULL, " ");
                dev_id = strtol(word, &endptr, 0);
	    }

            //check for strtol errors
            if (errno != 0)
            {
               perror("strtol");
               return -1;
            } 			   
            else if (endptr == word) 
            {
               fprintf(stderr, "No digits were found for lxlnx_hwdev option\n");
               return -1;
            }

            if ((dev_id < MAX_XLNX_DEVS) && (dev_id > -1))
            { 
                if (dev_list[dev_id] == false)
                {
                    pop_dev_list->vt_dev_ids[i] = (int) dev_id;
                    dev_list[dev_id] = true;
                    i++;
                    pop_dev_list->num_devs = i;					
                }
            }          
            else 
            {
                fprintf(stderr, "ERROR:Invalid device ID %d suppled to Xilinx device command line options.\n", dev_id);
                return -1;                                                \
            }
        }

        word = strtok (NULL, " ");
    }

    return 0;
    	
}

int xrm_props(xrmCuPoolPropertyV2* props, char* value, xrm_dev_list* pop_dev_list)
{
    int index = 0;
    uint64_t deviceInfoContraintType = XRM_DEVICE_INFO_CONSTRAINT_TYPE_VIRTUAL_DEVICE_INDEX;
    uint64_t deviceInfoDeviceIndex = 0;
    char pluginName[XRM_MAX_NAME_LEN];
    int func_id = 0, dec_load[2]={10,10}, scal_load[2]={10,10}, la_load[2], enc_load[2], enc_num[2], skip=0;
    char* endptr;

    //XRM plugin param populate
    xrmContext *ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
    xrmPluginFuncParam param;
    memset(&param, 0, sizeof(xrmPluginFuncParam));

    pt::ptree job;    
    pt::read_json(value,job);
    std::stringstream jobStr;
    boost::property_tree::write_json(jobStr, job);
    strncpy(param.input,jobStr.str().c_str(),MAX_CH_SIZE-1);    

    strcpy(pluginName, "xrmU30DecPlugin");
    if (xrmExecPluginFunc(ctx, pluginName, func_id, &param) != XRM_SUCCESS)
    {
       fprintf (stderr, "decoder plugin function=%d fail to run the function.\n",func_id);
       return -1;
    }
    else 
    {
       printf ( "decder plugin function , param_output=%s \n",param.output );
       for (int nd=0; nd< pop_dev_list->num_devs; nd++)
       {
           errno = 0;
           if (nd==0) 
              dec_load[nd] = (int) strtol((char*)(strtok(param.output, " ")), &endptr, 0); 
           else
              dec_load[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0); 
           
           printf ( "decder plugin function =%d success to run the function, output_load:%d\n",func_id,dec_load[nd]);
           if (dec_load[nd] > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
           {
               fprintf (stderr, "requested decder load =%d exceeds maximum capcity.\n",dec_load[nd]);
               return -1;
           }
           else 
               fprintf (stderr, "decder plugin function =%d success to run the function, output_load:%d\n",func_id,dec_load[nd]);

           skip = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0); //number of instances

           //check for strtol errors
           if (errno != 0)
           {
               perror("strtol");
               return -1;
           }
       }                                        
    }

    strcpy(pluginName, "xrmU30ScalPlugin");
    if (xrmExecPluginFunc(ctx, pluginName, func_id, &param) != XRM_SUCCESS)
    {
       fprintf (stderr, "scaler plugin function=%d fail to run the function.\n",func_id);		
       return -1;
    }			
    else 
    {
       printf ( "scaler plugin function , param_output=%s \n",param.output );
       for (int nd=0; nd<pop_dev_list->num_devs; nd++)
       {
           errno = 0;
           if (nd==0) 
              scal_load[nd] = (int) strtol((char*)(strtok(param.output, " ")), &endptr, 0);
           else 
              scal_load[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0);

           if (scal_load[nd] > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
           {
               fprintf (stderr, "requested scaler load =%d exceeds maximum capcity.\n",scal_load[nd]);
               return -1;
           }
           else
               fprintf (stderr, "scaler plugin function =%d success to run the function, output_load:%d\n",func_id,scal_load[nd]);
 
           skip = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0); //number of instances

            //check for strtol errors
            if (errno != 0)
            {
               perror("strtol");
               return -1;
            }
       }
    }

    strcpy(pluginName, "xrmU30EncPlugin");
    if (xrmExecPluginFunc(ctx, pluginName, func_id, &param) != XRM_SUCCESS)
    {
       fprintf (stderr, "encoder plugin function=%d fail to run the function.\n",func_id);
       return -1;
    }
    else
    {
       printf ( "encoder plugin function , param_output=%s \n",param.output );
       for (int nd=0; nd< pop_dev_list->num_devs; nd++)
       {
           errno = 0; 
           if (nd==0) 
              enc_load[nd] = (int) strtol((char*)(strtok(param.output, " ")), &endptr, 0);
           else 
              enc_load[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0);

           enc_num[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0);
 
           la_load[nd] = (int) strtol((char*)(strtok(NULL, " ")), &endptr, 0);

            //check for strtol errors
            if (errno != 0)
            {
               perror("strtol");
               return -1;
            }

           if (enc_load[nd] > XRM_MAX_CU_LOAD_GRANULARITY_1000000)
           {
               fprintf (stderr, "requested encoder load =%d exceeds maximum capcity.\n",enc_load[nd]);
               return -1;
           }
           else
               fprintf (stderr, "encoder plugin function =%d success to run the function, output_load:%d enc_num=%d la_load=%d\n",func_id,enc_load[nd],enc_num[nd],la_load[nd]);
       }
    }    

	
    for (int nd=0; nd<pop_dev_list->num_devs; nd++)
    {   
       pop_dev_list->dev_start_cuidx[pop_dev_list->vt_dev_ids[nd]] = index;
       
       if (dec_load[nd]> 0)
       {
          strcpy(props->cuListProp.cuProps[index].kernelName, "decoder");
          strcpy(props->cuListProp.cuProps[index].kernelAlias, "DECODER_MPSOC");
          props->cuListProp.cuProps[index].devExcl = false;
          props->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
          props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(dec_load[nd]);
          index++;
	
          strcpy(props->cuListProp.cuProps[index].kernelName, "kernel_vcu_decoder");
          strcpy(props->cuListProp.cuProps[index].kernelAlias, "");
          props->cuListProp.cuProps[index].devExcl = false;
          props->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
          props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
          index++;
       } 

       if (scal_load[nd] > 0)
       {
          strcpy(props->cuListProp.cuProps[index].kernelName, "scaler");
          strcpy(props->cuListProp.cuProps[index].kernelAlias, "SCALER_MPSOC");
          props->cuListProp.cuProps[index].devExcl = false;
          props->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) |
                                                                  (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
          props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(scal_load[nd]);
          index++;
       }
                                    
       if (enc_load[nd] > 0)
       {
          strcpy(props->cuListProp.cuProps[index].kernelName, "encoder");
          strcpy(props->cuListProp.cuProps[index].kernelAlias, "ENCODER_MPSOC");
          props->cuListProp.cuProps[index].devExcl = false;
          props->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
          props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(enc_load[nd]);
          index++;
       }
       for (int skrnl=0; skrnl<enc_num[nd]; skrnl++)
       {
          strcpy(props->cuListProp.cuProps[index].kernelName, "kernel_vcu_encoder");
          strcpy(props->cuListProp.cuProps[index].kernelAlias, "");
          props->cuListProp.cuProps[index].devExcl = false;
          props->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
          props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
          index++;
       }
                                    
       if (la_load[nd] > 0)
       {
          strcpy(props->cuListProp.cuProps[index].kernelName, "lookahead");
          strcpy(props->cuListProp.cuProps[index].kernelAlias, "LOOKAHEAD_MPSOC");

          props->cuListProp.cuProps[index].devExcl = false;
          props->cuListProp.cuProps[index].deviceInfo = (deviceInfoDeviceIndex << XRM_DEVICE_INFO_DEVICE_INDEX_SHIFT) | (deviceInfoContraintType << XRM_DEVICE_INFO_CONSTRAINT_TYPE_SHIFT);
          props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(la_load[nd]);
          index++;
       }
       deviceInfoDeviceIndex = ++deviceInfoDeviceIndex;

    }
								
    props->cuListProp.cuNum = index;
    
    //destroying XRM context
    if(xrmDestroyContext(ctx) != XRM_SUCCESS)
       fprintf (stderr, "XRM destroy context failed! \n");	

    return 0;
}

int fill_props (int file, xrmCuPoolPropertyV2* props, char* cmdline, xrm_dev_list* pop_dev_list)
{
    PARSER_RET ret;
    int index = 0;
    char key[128];
    char value[MAX_CH_SIZE];
    char job_name[MAX_CH_SIZE];

    memset (props, 0, sizeof (*props));
    if (cmdline)
        cmdline[0] = '\0';

    int cp_file =file;

    props->cuListNum = 1;

    do
    {
        ret = get_key_value_pair (file, key, value);
        if (ret == PARSER_RET_EOL)
           continue;

        if (ret == PARSER_RET_ERROR)
        {
           fprintf (stderr, "Parsing config file failed!\n");
           return -1;
        }
        if (ret == PARSER_RET_SUCCESS)
        {
           if (strcmp(key, "job_description") == 0)
           {
              strcpy(job_name, value);
           }
           else 
           {
              if (strcmp (key, "cmdline") == 0)
              {
                  if (cmdline)
                  {						
                     strcpy(cmdline, value);

                    //get virtual dev id's from ffmpeg command
                    ret = get_ffmpeg_cmd_vt_devs(value, pop_dev_list);	
                    if (ret == -1)
                       return ret;
                  }						
              } 
              else
              {
                  fprintf (stderr, "Parsing config file failed! Unknown key \"%s\"\n", key);
                  return -1;
              }	
           }
        }

    } while (ret != PARSER_RET_EOF);

    ret = xrm_props(props, job_name, pop_dev_list);
    if (ret < 0) return -1;
	
    if ( props->cuListProp.cuNum == 0)
    {
        fprintf (stderr, "Parsing description job failed!\n");
        return -1;
    }
      
    return 0;
}
