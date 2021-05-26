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

int fill_props (int file, xrmCuPoolProperty* props, char* cmdline)
{
	memset (props, 0, sizeof (*props));
	if (cmdline)
		cmdline[0] = '\0';

	props->cuListProp.sameDevice = 1;
        props->cuListNum = 1;
	
	PARSER_RET ret;
	int index = 0;
	do
	{
		char key[128];
		char value[MAX_CH_SIZE];
		ret = get_key_value_pair (file, key, value);
		if (ret == PARSER_RET_EOL)
			continue;
		if (ret == PARSER_RET_ERROR)
		{
			printf("Parsing config file failed!\n");
			return -1;
		}
		if (ret == PARSER_RET_SUCCESS)
		{
			if (strcmp(key, "job_description") == 0)
			{

	                   	/****************************************************/
				//XRM plugin param populate
				xrmContext *ctx = (xrmContext *)xrmCreateContext(XRM_API_VERSION_1);
				xrmPluginFuncParam param;
				memset(&param, 0, sizeof(xrmPluginFuncParam));
				char pluginName[XRM_MAX_NAME_LEN];
				int func_id = 0, dec_load=0, scal_load=0, la_load=0, enc_load=0, enc_num=0;
				pt::ptree job;    
                                //printf("job name=%s\n",value);
				pt::read_json(value,job);

				std::stringstream jobStr;
				boost::property_tree::write_json(jobStr, job);

				strncpy(param.input,jobStr.str().c_str(),MAX_CH_SIZE-1);    
 
				strcpy(pluginName, "xrmU30DecPlugin");
				if (xrmExecPluginFunc(ctx, pluginName, func_id, &param) != XRM_SUCCESS)
                                {
				       printf("decoder plugin function=%d fail to run the function.\n",func_id);
                                       return -1;
                                }
				else 
				{
                                        dec_load = atoi((char*)(strtok(param.output, " "))); 
                                        if (dec_load> XRM_MAX_CU_LOAD_GRANULARITY_1000000)
                                        {
                                           printf("requested decder load =%d exceeds maximum capcity.\n",dec_load);
                                           return -1;
                                        }
                                        else 
   					   printf("decder plugin function =%d success to run the function, output_load:%d\n",func_id,dec_load);
				}

				strcpy(pluginName, "xrmU30ScalPlugin");
				if (xrmExecPluginFunc(ctx, pluginName, func_id, &param) != XRM_SUCCESS)
                                {
					printf("scaler plugin function=%d fail to run the function.\n",func_id);		
                                        return -1;
                                }			
				else 
				{
                                        scal_load = atoi((char*)(strtok(param.output, " ")));
                                        if (scal_load> XRM_MAX_CU_LOAD_GRANULARITY_1000000)
                                        {
                                           printf("requested scaler load =%d exceeds maximum capcity.\n",scal_load);
                                           return -1;
                                        }
                                        else
  					   printf("scaler plugin function =%d success to run the function, output_load:%d\n",func_id,scal_load);					
				}

				strcpy(pluginName, "xrmU30EncPlugin");
				if (xrmExecPluginFunc(ctx, pluginName, func_id, &param) != XRM_SUCCESS)
                                {
					printf("encoder plugin function=%d fail to run the function.\n",func_id);
                                        return -1;
                                }
				else
				{
                                        enc_load = atoi((char*)(strtok(param.output, " ")));
                                        enc_num = atoi((char*)(strtok(NULL, " ")));

                                        la_load = atoi((char*)(strtok(NULL, " ")));
                                        if (enc_load> XRM_MAX_CU_LOAD_GRANULARITY_1000000)
                                        {
                                           printf("requested encoder load =%d exceeds maximum capcity.\n",enc_load);
                                           return -1;
                                         }
                                         else
                         		    printf("encoder plugin function =%d success to run the function, output_load:%d enc_num=%d la_load=%d\n",func_id,enc_load,enc_num,la_load);
				}    

				/****************************************************/


				if (dec_load> 0)
				{
					strcpy(props->cuListProp.cuProps[index].kernelName, "decoder");
					strcpy(props->cuListProp.cuProps[index].kernelAlias, "DECODER_MPSOC");
					props->cuListProp.cuProps[index].devExcl = false;
					props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(dec_load);
                                        index++;
				        strcpy(props->cuListProp.cuProps[index].kernelName, "kernel_vcu_decoder");
				        strcpy(props->cuListProp.cuProps[index].kernelAlias, "");
				        props->cuListProp.cuProps[index].devExcl = false;
				        props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
                                        index++;
				} 
				if (scal_load > 0)
				{
					strcpy(props->cuListProp.cuProps[index].kernelName, "scaler");
					strcpy(props->cuListProp.cuProps[index].kernelAlias, "SCALER_MPSOC");
					props->cuListProp.cuProps[index].devExcl = false;
					props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(scal_load);
                                        index++;
				}
				if (enc_load > 0)
				{
					strcpy(props->cuListProp.cuProps[index].kernelName, "encoder");
					strcpy(props->cuListProp.cuProps[index].kernelAlias, "ENCODER_MPSOC");
					props->cuListProp.cuProps[index].devExcl = false;
					props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(enc_load);
                                        index++;

                                        for (int skrnl=0; skrnl<enc_num; skrnl++)
                                        {
				        strcpy(props->cuListProp.cuProps[index].kernelName, "kernel_vcu_encoder");
				        strcpy(props->cuListProp.cuProps[index].kernelAlias, "");
				        props->cuListProp.cuProps[index].devExcl = false;
		        		props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(XRM_MAX_CU_LOAD_GRANULARITY_1000000);
                                        index++;
                                        }
                                }
				if (la_load > 0)
				{
                                        //Look ahead
					strcpy(props->cuListProp.cuProps[index].kernelName, "lookahead");
					strcpy(props->cuListProp.cuProps[index].kernelAlias, "LOOKAHEAD_MPSOC");
					props->cuListProp.cuProps[index].devExcl = false;
					props->cuListProp.cuProps[index].requestLoad = XRM_PRECISION_1000000_BIT_MASK(la_load);
                                        index++;

				}
                                //destroying XRM context
			        if(xrmDestroyContext(ctx) != XRM_SUCCESS)
			            printf("XRM destroy context failed! \n");
			}
			else 
			{
				if (strcmp (key, "cmdline") == 0)
				{
					if (cmdline)
						strcpy(cmdline, value);
				} else
				{
					printf("Parsing config file failed! Unknown key \"%s\"\n", key);
					return -1;
				}	
			}
			props->cuListProp.cuNum = index;
		}

	} while (ret != PARSER_RET_EOF);
	
	if ( props->cuListProp.cuNum == 0)
	{
		printf("Parsing description job failed!\n");
		return -1;
	}      
	
	return 0;
}
