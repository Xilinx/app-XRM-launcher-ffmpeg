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
#ifndef _PARSER_H_
#define _PARSER_H_

#include <stdbool.h>
#include </opt/xilinx/xrm/include/xrm.h>
#include <errno.h>

extern "C" {
	#include <pthread.h>
}

#define MAX_XLNX_DEVS 128


typedef enum PARSER_RET
{
	PARSER_RET_SUCCESS,
	PARSER_RET_EOF,
	PARSER_RET_EOL,
	PARSER_RET_ERROR
} PARSER_RET;

typedef struct xrm_dev_list
{
    int num_devs; 
    int hw_dev_ids[MAX_XLNX_DEVS];
    int vt_dev_ids[MAX_XLNX_DEVS];	
    int dev_start_cuidx[MAX_XLNX_DEVS];	
}xrm_dev_list;

PARSER_RET get_line (int file, char* value);
int fill_props (int file, xrmCuPoolPropertyV2* props, char* cmdline, xrm_dev_list* pop_dev_list);

#endif /* _PARSER_H_ */
