/*
 * global_variables.h
 *
 *  Created on: Dec 31, 2022
 *      Author: Vlad
 */

#ifndef SRC_GLOBAL_VARIABLES_H_
#define SRC_GLOBAL_VARIABLES_H_

#define GV_READONLY 1
#define GV_WRITONLY 2
#define GV_WR 3

#define SET_TIME(x) (G_Variables[0].value=(x))
#define GET_TIME() (G_Variables[0].value)

extern const char no_VariableDefitions;

typedef struct
{
	char name[25];
	char flags;
	int value;
}VariableDefition;

extern VariableDefition GV_Variables[];
void update_gv(void);

#endif /* SRC_GLOBAL_VARIABLES_H_ */
