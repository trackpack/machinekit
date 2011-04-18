/********************************************************************
* Description: interp_execute.cc
*
*   Derived from a work by Thomas Kramer
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
* Last change:
********************************************************************/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "rs274ngc.hh"
#include "rs274ngc_return.hh"
#include "interp_internal.hh"
#include "rs274ngc_interp.hh"

#define RESULT_OK(x) ((x) == INTERP_OK || (x) == INTERP_EXECUTE_FINISH)

/****************************************************************************/

/*! execute binary

Returned value: int
   If execute_binary1 or execute_binary2 returns an error code, this
   returns that code.
   Otherwise, it returns INTERP_OK.

Side effects: The value of left is set to the result of applying
  the operation to left and right.

Called by: read_real_expression

This just calls either execute_binary1 or execute_binary2.

*/

int Interp::execute_binary(double *left, int operation, double *right)
{
  if (operation < AND2)
    CHP(execute_binary1(left, operation, right));
  else
    CHP(execute_binary2(left, operation, right));
  return INTERP_OK;
}

/****************************************************************************/

/*! execute_binary1

Returned Value: int
   If any of the following errors occur, this returns the error shown.
   Otherwise, it returns INTERP_OK.
   1. operation is unknown: NCE_BUG_UNKNOWN_OPERATION
   2. An attempt is made to divide by zero: NCE_ATTEMPT_TO_DIVIDE_BY_ZERO
   3. An attempt is made to raise a negative number to a non-integer power:
      NCE_ATTEMPT_TO_RAISE_NEGATIVE_TO_NON_INTEGER_POWER

Side effects:
   The result from performing the operation is put into what left points at.

Called by: read_real_expression.

This executes the operations: DIVIDED_BY, MODULO, POWER, TIMES.

*/

int Interp::execute_binary1(double *left,        //!< pointer to the left operand    
                           int operation,       //!< integer code for the operation 
                           double *right)       //!< pointer to the right operand   
{
  switch (operation) {
  case DIVIDED_BY:
    CHKS((*right == 0.0), NCE_ATTEMPT_TO_DIVIDE_BY_ZERO);
    *left = (*left / *right);
    break;
  case MODULO:                 /* always calculates a positive answer */
    *left = fmod(*left, *right);
    if (*left < 0.0) {
      *left = (*left + fabs(*right));
    }
    break;
  case POWER:
    CHKS(((*left < 0.0) && (floor(*right) != *right)),
        NCE_ATTEMPT_TO_RAISE_NEGATIVE_TO_NON_INTEGER_POWER);
    *left = pow(*left, *right);
    break;
  case TIMES:
    *left = (*left * *right);
    break;
  default:
    ERS(NCE_BUG_UNKNOWN_OPERATION);
  }
  return INTERP_OK;
}

/****************************************************************************/

/*! execute_binary2

Returned Value: int
   If any of the following errors occur, this returns the error code shown.
   Otherwise, it returns INTERP_OK.
   1. operation is unknown: NCE_BUG_UNKNOWN_OPERATION

Side effects:
   The result from performing the operation is put into what left points at.

Called by: read_real_expression.

This executes the operations: AND2, EXCLUSIVE_OR, MINUS,
NON_EXCLUSIVE_OR, PLUS. The RS274/NGC manual [NCMS] does not say what
the calculated value of the three logical operations should be. This
function calculates either 1.0 (meaning true) or 0.0 (meaning false).
Any non-zero input value is taken as meaning true, and only 0.0 means
false.


*/

int Interp::execute_binary2(double *left,        //!< pointer to the left operand    
                           int operation,       //!< integer code for the operation 
                           double *right)       //!< pointer to the right operand   
{
  double diff;
  switch (operation) {
  case AND2:
    *left = ((*left == 0.0) || (*right == 0.0)) ? 0.0 : 1.0;
    break;
  case EXCLUSIVE_OR:
    *left = (((*left == 0.0) && (*right != 0.0))
             || ((*left != 0.0) && (*right == 0.0))) ? 1.0 : 0.0;
    break;
  case MINUS:
    *left = (*left - *right);
    break;
  case NON_EXCLUSIVE_OR:
    *left = ((*left != 0.0) || (*right != 0.0)) ? 1.0 : 0.0;
    break;
  case PLUS:
    *left = (*left + *right);
    break;

  case LT:
      *left = (*left < *right) ? 1.0 : 0.0;
      break;
  case EQ:
      diff = *left - *right;
      diff = (diff < 0) ? -diff : diff;
      *left = (diff < TOLERANCE_EQUAL) ? 1.0 : 0.0;
      break;
  case NE:
      diff = *left - *right;
      diff = (diff < 0) ? -diff : diff;
      *left = (diff >= TOLERANCE_EQUAL) ? 1.0 : 0.0;
      break;
  case LE:
      *left = (*left <= *right) ? 1.0 : 0.0;
      break;
  case GE:
      *left = (*left >= *right) ? 1.0 : 0.0;
      break;
  case GT:
      *left = (*left > *right) ? 1.0 : 0.0;
      break;

  default:
    ERS(NCE_BUG_UNKNOWN_OPERATION);
  }
  return INTERP_OK;
}

int Interp::report_error(setup_pointer settings,int status,const char *text)
{
    char interp_error_text_buf[LINELEN],msg[LINELEN];
    char interp_stack_buf[LINELEN];

    if (!RESULT_OK(status)) {
	if (status  > INTERP_MIN_ERROR) {
	    error_text(status, interp_error_text_buf, LINELEN);
	    snprintf(msg,sizeof(msg),
		     "%s:%d: \"%s\" - %s (%s)",
		     settings->filename,
		     sequence_number(),
		     text,
		     interp_error_text_buf,
		     interp_status(status));
	    MESSAGE(msg);
	} else {
	    snprintf(msg,sizeof(msg),
		     "%s:%d: \"%s\" (%s)",
		     settings->filename,
		     sequence_number(),
		     text,
		     interp_status(status));
	    MESSAGE(msg);
	}
    }
    // from emctask.cc:print_interp_error(int retval)
    int index = 0;
    int traceback = 0;  // very intrusive
    if (traceback &&  (status  > INTERP_MIN_ERROR)) {
	while (index < 5) {
	    interp_stack_buf[0] = 0;
	    stack_name(index, interp_stack_buf, LINELEN);
	    if (0 == interp_stack_buf[0]) {
		break;
	    }
	    snprintf(msg,sizeof(msg),"%d: %s",index,interp_stack_buf);
	    MESSAGE(msg);
	    index++;
	}
    }
    return status;
}

// Tx epiplogue - executed past T_COMMAND
int Interp::finish_t_command(setup_pointer settings)
{

    // if T_COMMAND 'return'ed or 'endsub'ed a #<_value> >= 0,
    // commit the tool prepare to that value.

    if (settings->return_value > - TOLERANCE_EQUAL) {
	settings->selected_pocket = round_to_int(settings->return_value);
	SELECT_POCKET(settings->selected_pocket);
    } else {
	CANON_ERROR("T<tool> - prepare failed (%f)", settings->return_value);
	SEND_HANDLER_ABORT(round_to_int(settings->return_value));
	return INTERP_OK;
    }
    return(INTERP_OK);
}


// common code for T_COMMAND, M6_COMMAND, ON_ABORT handlers
int Interp::execute_handler(setup_pointer settings, const char *cmd,
			    int (Interp::*epilog)(setup_pointer settings),
			    int remap_op
)
{
    // TBD:  good error reporting on errors in T_COMMAND, M6_COMMAND

    settings->sequence_number = 1;

    // Gcode handlers need some c/c++ to finish work after doing theirs.
    // that's what epilogue functions are for.

    // this tells O_call code to mark the frame with an epilogue handler
    // the corresponding O_endsub/O_return will call the epilog
    // this is zeroed when next O_call is executed
    // it's essentially a hidden param to the call

    settings->epilog_hook = epilog;
    // just read and parse into _setup.block1
    // NB: we're NOT triggering MDI handling in execute()
    int status = read(cmd);
    CHP(status);
    // status = execute_remap(NULL,0,remap_op);
    // return(status);
}


/****************************************************************************/

/*! execute_block

Returned Value: int
   If convert_stop returns INTERP_EXIT, this returns INTERP_EXIT.
   If any of the following functions is called and returns an error code,
   this returns that code.
     convert_comment
     convert_feed_mode
     convert_feed_rate
     convert_g
     convert_m
     convert_speed
     convert_stop
     convert_tool_select
   Otherwise, if the probe_flag in the settings is true, 
   or the input_flag is set to true this returns
      INTERP_EXECUTE_FINISH.
   Otherwise, it returns INTERP_OK.

Side effects:
   One block of RS274/NGC instructions is executed.

Called by:
   Interp::execute

This converts a block to zero to many actions. The order of execution
of items in a block is critical to safe and effective machine operation,
but is not specified clearly in the RS274/NGC documentation.

Actions are executed in the following order:
1. any comment.
2. a feed mode setting (g93, g94, g95)
3. a feed rate (f) setting if in units_per_minute feed mode.
4. a spindle speed (s) setting.
5. a tool selection (t).
6. "m" commands as described in convert_m (includes tool change).
7. any g_codes (except g93, g94) as described in convert_g.
8. stopping commands (m0, m1, m2, m30, or m60).

In inverse time feed mode, the explicit and implicit g code executions
include feed rate setting with g1, g2, and g3. Also in inverse time
feed mode, attempting a canned cycle cycle (g81 to g89) or setting a
feed rate with g0 is illegal and will be detected and result in an
error message.

*/

int Interp::execute_block(block_pointer block,   //!< pointer to a block of RS274/NGC instructions
			  setup_pointer settings, //!< pointer to machine settings
			  bool remove_trail)
{
  int status;

  block->line_number = settings->sequence_number;
  if (block->comment[0] != 0) {
    status = convert_comment(block->comment);
    if (remove_trail)
	block->comment[0] = 0;
    CHP(status);
  }
  if (block->g_modes[GM_SPINDLE_MODE] != -1) {
      status = convert_spindle_mode(block, settings);
      if (remove_trail)
	  block->g_modes[GM_SPINDLE_MODE] = -1;
      CHP(status);
  }
  if (block->g_modes[GM_FEED_MODE] != -1) {
      status = convert_feed_mode(block->g_modes[GM_FEED_MODE], settings);
     if (remove_trail)
	  block->g_modes[GM_FEED_MODE] = -1;
      CHP(status);

  }
  if (block->f_flag) {
    if (settings->feed_mode != INVERSE_TIME) {
	status = convert_feed_rate(block, settings);
	if (remove_trail)
	    block->f_flag = false;
	CHP(status);
    }
    /* INVERSE_TIME is handled elsewhere */
  }
  if (block->s_flag) {
      status = convert_speed(block, settings);
	if (remove_trail)
	    block->s_flag = false;
	CHP(status);
  }
  if (block->t_flag) {
    if (settings->t_command) {
	char cmd[LINELEN];
	int pocket;
	if (remove_trail)
	    block->t_flag = false;
	CHP((find_tool_pocket(settings, block->t_number, &pocket)));

	// pocket will start making sense once tooltable I/O is folded into
	// interp and iocontrol is gone.
	snprintf(cmd,sizeof(cmd), "%s [%d] [%d]",
		 settings->t_command,
		 block->t_number,
		 pocket);

	status = execute_handler(settings, cmd, &Interp::finish_t_command, T_REMAP);
	// fprintf(stderr,"---- execute_block(t_command) returning %s\n",
	// 	interp_status(status));

	return (-T_REMAP);
	CHP(status);

    } else {
	CHP(convert_tool_select(block, settings));
    }
  }
  CHP(convert_m(block, settings, remove_trail));
  CHP(convert_g(block, settings, remove_trail));
  if (block->m_modes[4] != -1) {        /* converts m0, m1, m2, m30, or m60 */
    status = convert_stop(block, settings);
    if (status == INTERP_EXIT) {
	return(INTERP_EXIT);
    }
    else if (status != INTERP_OK) {
	ERP(status);
    }
  }
  if (settings->probe_flag)
      return (INTERP_EXECUTE_FINISH);

  if (settings->input_flag)
      return (INTERP_EXECUTE_FINISH);

  if (settings->toolchange_flag)
      return (INTERP_EXECUTE_FINISH);

  return INTERP_OK;
}

/****************************************************************************/

/*! execute_unary

Returned Value: int
   If any of the following errors occur, this returns the error code shown.
   Otherwise, it returns INTERP_OK.
   1. the operation is unknown: NCE_BUG_UNKNOWN_OPERATION
   2. the argument to acos is not between minus and plus one:
      NCE_ARGUMENT_TO_ACOS_OUT_RANGE
   3. the argument to asin is not between minus and plus one:
      NCE_ARGUMENT_TO_ASIN_OUT_RANGE
   4. the argument to the natural logarithm is not positive:
      NCE_ZERO_OR_NEGATIVE_ARGUMENT_TO_LN
   5. the argument to square root is negative:
      NCE_NEGATIVE_ARGUMENT_TO_SQRT

Side effects:
   The result from performing the operation on the value in double_ptr
   is put into what double_ptr points at.

Called by: read_unary.

This executes the operations: ABS, ACOS, ASIN, COS, EXP, FIX, FUP, LN
ROUND, SIN, SQRT, TAN

All angle measures in the input or output are in degrees.

*/

int Interp::execute_unary(double *double_ptr,    //!< pointer to the operand         
                         int operation) //!< integer code for the operation 
{
  switch (operation) {
  case ABS:
    if (*double_ptr < 0.0)
      *double_ptr = (-1.0 * *double_ptr);
    break;
  case ACOS:
    CHKS(((*double_ptr < -1.0) || (*double_ptr > 1.0)),
        NCE_ARGUMENT_TO_ACOS_OUT_OF_RANGE);
    *double_ptr = acos(*double_ptr);
    *double_ptr = ((*double_ptr * 180.0) / M_PIl);
    break;
  case ASIN:
    CHKS(((*double_ptr < -1.0) || (*double_ptr > 1.0)),
        NCE_ARGUMENT_TO_ASIN_OUT_OF_RANGE);
    *double_ptr = asin(*double_ptr);
    *double_ptr = ((*double_ptr * 180.0) / M_PIl);
    break;
  case COS:
    *double_ptr = cos((*double_ptr * M_PIl) / 180.0);
    break;
  case EXISTS:
    // do nothing here
    // result for the EXISTS function is set by Interp:read_unary()
    break;
  case EXP:
    *double_ptr = exp(*double_ptr);
    break;
  case FIX:
    *double_ptr = floor(*double_ptr);
    break;
  case FUP:
    *double_ptr = ceil(*double_ptr);
    break;
  case LN:
    CHKS((*double_ptr <= 0.0), NCE_ZERO_OR_NEGATIVE_ARGUMENT_TO_LN);
    *double_ptr = log(*double_ptr);
    break;
  case ROUND:
    *double_ptr = (double)
      ((int) (*double_ptr + ((*double_ptr < 0.0) ? -0.5 : 0.5)));
    break;
  case SIN:
    *double_ptr = sin((*double_ptr * M_PIl) / 180.0);
    break;
  case SQRT:
    CHKS((*double_ptr < 0.0), NCE_NEGATIVE_ARGUMENT_TO_SQRT);
    *double_ptr = sqrt(*double_ptr);
    break;
  case TAN:
    *double_ptr = tan((*double_ptr * M_PIl) / 180.0);
    break;
  default:
    ERS(NCE_BUG_UNKNOWN_OPERATION);
  }
  return INTERP_OK;
}

