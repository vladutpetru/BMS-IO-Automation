/*
 * global_variables.c
 *
 *  Created on: Dec 31, 2022
 *      Author: Vlad
 */
#include "global_variables.h"


__attribute__((weak)) VariableDefition GV_Variables[] =
{
		{
				"Time",
				GV_READONLY,
				0,
		},
		{
				"Dummy",
				GV_WR,
				0,
		}
};


__attribute__((weak)) const char no_VariableDefitions = sizeof(GV_Variables)/sizeof(GV_Variables[0]);