/* Copyright (c) 2008-2013 Da SimGrid Team. All rights reserved.            */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>

#include "simgrid/sg_config.h"
#include "../surf/surf_private.h"
#include "../simix/smx_private.h"
#include "../xbt/mmalloc/mmprivate.h"
#include "xbt/fifo.h"
#include "mc_private.h"
#include "xbt/automaton.h"
#include "xbt/dict.h"

XBT_LOG_NEW_CATEGORY(mc, "All MC categories");
XBT_LOG_NEW_DEFAULT_SUBCATEGORY(mc_global, mc,
                                "Logging specific to MC (global)");

/* Configuration support */
e_mc_reduce_t mc_reduce_kind=e_mc_reduce_unset;

int _sg_do_model_check = 0;
int _sg_mc_checkpoint=0;
char* _sg_mc_property_file=NULL;
int _sg_mc_timeout=0;
int _sg_mc_max_depth=1000;
int _sg_mc_visited=0;
char *_sg_mc_dot_output_file = NULL;

extern int _sg_init_status;
void _mc_cfg_cb_reduce(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a reduction strategy after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  char *val= xbt_cfg_get_string(_sg_cfg_set, name);
  if (!strcasecmp(val,"none")) {
    mc_reduce_kind = e_mc_reduce_none;
  } else if (!strcasecmp(val,"dpor")) {
    mc_reduce_kind = e_mc_reduce_dpor;
  } else {
    xbt_die("configuration option %s can only take 'none' or 'dpor' as a value",name);
  }
}

void _mc_cfg_cb_checkpoint(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a checkpointing value after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  _sg_mc_checkpoint = xbt_cfg_get_boolean(_sg_cfg_set, name);
}
void _mc_cfg_cb_property(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a property after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  _sg_mc_property_file= xbt_cfg_get_string(_sg_cfg_set, name);
}

void _mc_cfg_cb_timeout(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a value to enable/disable timeout for wait requests after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  _sg_mc_timeout= xbt_cfg_get_boolean(_sg_cfg_set, name);
}

void _mc_cfg_cb_max_depth(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a max depth value after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  _sg_mc_max_depth= xbt_cfg_get_int(_sg_cfg_set, name);
}

void _mc_cfg_cb_visited(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a number of stored visited states after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  _sg_mc_visited= xbt_cfg_get_int(_sg_cfg_set, name);
}

void _mc_cfg_cb_dot_output(const char *name, int pos) {
  if (_sg_init_status && !_sg_do_model_check) {
    xbt_die("You are specifying a file name for a dot output of graph state after the initialization (through MSG_config?), but model-checking was not activated at config time (through --cfg=model-check:1). This won't work, sorry.");
  }
  _sg_mc_dot_output_file= xbt_cfg_get_string(_sg_cfg_set, name);
}

/* MC global data structures */

mc_state_t mc_current_state = NULL;
char mc_replay_mode = FALSE;
double *mc_time = NULL;
mc_comparison_times_t mc_comp_times = NULL;
double mc_snapshot_comparison_time;
mc_stats_t mc_stats = NULL;

/* Safety */

xbt_fifo_t mc_stack_safety = NULL;
mc_global_t initial_state_safety = NULL;

/* Liveness */

xbt_fifo_t mc_stack_liveness = NULL;
mc_global_t initial_state_liveness = NULL;
int compare;

/* Local */
xbt_dict_t mc_local_variables = NULL;
/* Global */
xbt_dynar_t mc_global_variables = NULL;

/* Ignore mechanism */
xbt_dynar_t mc_stack_comparison_ignore;
xbt_dynar_t mc_data_bss_comparison_ignore;
extern xbt_dynar_t mc_heap_comparison_ignore;
extern xbt_dynar_t stacks_areas;

FILE *dot_output = NULL;
const char* colors[13];

xbt_automaton_t _mc_property_automaton = NULL;

/* Static functions */

static void MC_assert_pair(int prop);
static dw_location_t get_location(xbt_dict_t location_list, char *expr);
static dw_frame_t get_frame_by_offset(xbt_dict_t all_variables, unsigned long int offset);
static size_t data_bss_ignore_size(void *address);
static void MC_get_global_variables(char *elf_file);

void MC_do_the_modelcheck_for_real() {

  MC_SET_RAW_MEM;
  mc_comp_times = xbt_new0(s_mc_comparison_times_t, 1);
  MC_UNSET_RAW_MEM;
  
  if (!_sg_mc_property_file || _sg_mc_property_file[0]=='\0') {
    if (mc_reduce_kind==e_mc_reduce_unset)
      mc_reduce_kind=e_mc_reduce_dpor;

    XBT_INFO("Check a safety property");
    MC_modelcheck_safety();

  } else  {

    if (mc_reduce_kind==e_mc_reduce_unset)
      mc_reduce_kind=e_mc_reduce_none;

    XBT_INFO("Check the liveness property %s",_sg_mc_property_file);
    MC_automaton_load(_sg_mc_property_file);
    MC_modelcheck_liveness();
  }
}


void MC_compare(void){
  compare = 1;
}

void MC_init(){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);
  
  compare = 0;

  /* Initialize the data structures that must be persistent across every
     iteration of the model-checker (in RAW memory) */

  MC_SET_RAW_MEM;

  MC_init_memory_map_info();
  
  mc_local_variables = xbt_dict_new_homogeneous(NULL);

  /* Get local variables in binary for state equality detection */
  xbt_dict_t binary_location_list = MC_get_location_list(xbt_binary_name);
  MC_get_local_variables(xbt_binary_name, binary_location_list, &mc_local_variables);

  /* Get local variables in libsimgrid for state equality detection */
  xbt_dict_t libsimgrid_location_list = MC_get_location_list(libsimgrid_path);
  MC_get_local_variables(libsimgrid_path, libsimgrid_location_list, &mc_local_variables);

  xbt_dict_free(&libsimgrid_location_list);
  xbt_dict_free(&binary_location_list);
  
  /* Get .plt section (start and end addresses) for data libsimgrid and data program comparison */
  get_libsimgrid_plt_section();
  get_binary_plt_section();

  /* Get global variables */
  MC_get_global_variables(xbt_binary_name);
  MC_get_global_variables(libsimgrid_path);

  MC_UNSET_RAW_MEM;

   /* Ignore some variables from xbt/ex.h used by exception e for stacks comparison */
  MC_ignore_stack("e", "*");
  MC_ignore_stack("__ex_cleanup", "*");
  MC_ignore_stack("__ex_mctx_en", "*");
  MC_ignore_stack("__ex_mctx_me", "*");
  MC_ignore_stack("__xbt_ex_ctx_ptr", "*");
  MC_ignore_stack("_log_ev", "*");
  MC_ignore_stack("_throw_ctx", "*");
  MC_ignore_stack("ctx", "*");

  MC_ignore_stack("next_context", "smx_ctx_sysv_suspend_serial");
  MC_ignore_stack("i", "smx_ctx_sysv_suspend_serial");

  /* Ignore local variable about time used for tracing */
  MC_ignore_stack("start_time", "*"); 

  MC_ignore_data_bss(&mc_comp_times, sizeof(mc_comp_times));
  MC_ignore_data_bss(&mc_snapshot_comparison_time, sizeof(mc_snapshot_comparison_time)); 
  MC_ignore_data_bss(&mc_time, sizeof(mc_time));

  if(raw_mem_set)
    MC_SET_RAW_MEM;

}

void MC_init_dot_output(){ /* FIXME : more colors */

  colors[0] = "blue";
  colors[1] = "red";
  colors[2] = "green3";
  colors[3] = "goldenrod";
  colors[4] = "brown";
  colors[5] = "purple";
  colors[6] = "magenta";
  colors[7] = "turquoise4";
  colors[8] = "gray25";
  colors[9] = "forestgreen";
  colors[10] = "hotpink";
  colors[11] = "lightblue";
  colors[12] = "tan";

  dot_output = fopen(_sg_mc_dot_output_file, "w");
  
  if(dot_output == NULL){
    perror("Error open dot output file");
    xbt_abort();
  }

  fprintf(dot_output, "digraph graphname{\n fixedsize=true; rankdir=TB; ranksep=.25; edge [fontsize=12]; node [fontsize=10, shape=circle,width=.5 ]; graph [resolution=20, fontsize=10];\n");

}

void MC_modelcheck_safety(void)
{
  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  /* Check if MC is already initialized */
  if (initial_state_safety)
    return;

  mc_time = xbt_new0(double, simix_process_maxpid);

  /* mc_time refers to clock for each process -> ignore it for heap comparison */  
  MC_ignore_heap(mc_time, simix_process_maxpid * sizeof(double));

  /* Initialize the data structures that must be persistent across every
     iteration of the model-checker (in RAW memory) */
  
  MC_SET_RAW_MEM;

  /* Initialize statistics */
  mc_stats = xbt_new0(s_mc_stats_t, 1);
  mc_stats->state_size = 1;

  /* Create exploration stack */
  mc_stack_safety = xbt_fifo_new();

  if((_sg_mc_dot_output_file != NULL) && (_sg_mc_dot_output_file[0]!='\0'))
    MC_init_dot_output();

  MC_UNSET_RAW_MEM;

  if(_sg_mc_visited > 0){
    MC_init();
  }else{
    MC_SET_RAW_MEM;
    MC_init_memory_map_info();
    get_libsimgrid_plt_section();
    get_binary_plt_section();
    MC_UNSET_RAW_MEM;
  }

  MC_dpor_init();

  MC_SET_RAW_MEM;
  /* Save the initial state */
  initial_state_safety = xbt_new0(s_mc_global_t, 1);
  initial_state_safety->snapshot = MC_take_snapshot();
  MC_UNSET_RAW_MEM;

  MC_dpor();

  if(raw_mem_set)
    MC_SET_RAW_MEM;

  MC_exit();
}

void MC_modelcheck_liveness(){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_init();

  mc_time = xbt_new0(double, simix_process_maxpid);

  /* mc_time refers to clock for each process -> ignore it for heap comparison */  
  MC_ignore_heap(mc_time, simix_process_maxpid * sizeof(double));
 
  MC_SET_RAW_MEM;
 
  /* Initialize statistics */
  mc_stats = xbt_new0(s_mc_stats_t, 1);
  mc_stats->state_size = 1;

  /* Create exploration stack */
  mc_stack_liveness = xbt_fifo_new();

  /* Create the initial state */
  initial_state_liveness = xbt_new0(s_mc_global_t, 1);

  if((_sg_mc_dot_output_file != NULL) && (_sg_mc_dot_output_file[0]!='\0'))
    MC_init_dot_output();
  
  MC_UNSET_RAW_MEM;

  MC_ddfs_init();

  /* We're done */
  MC_print_statistics(mc_stats);
  xbt_free(mc_time);

  if(raw_mem_set)
    MC_SET_RAW_MEM;

}


void MC_exit(void)
{
  xbt_free(mc_time);
  MC_memory_exit();
  xbt_abort();
}

int SIMIX_pre_mc_random(smx_simcall_t simcall){

  return simcall->mc_value;
}


int MC_random(void)
{
  /*FIXME: return mc_current_state->executed_transition->random.value;*/
  return simcall_mc_random();
}

/**
 * \brief Schedules all the process that are ready to run
 */
void MC_wait_for_requests(void)
{
  smx_process_t process;
  smx_simcall_t req;
  unsigned int iter;

  while (!xbt_dynar_is_empty(simix_global->process_to_run)) {
    SIMIX_process_runall();
    xbt_dynar_foreach(simix_global->process_that_ran, iter, process) {
      req = &process->simcall;
      if (req->call != SIMCALL_NONE && !MC_request_is_visible(req))
        SIMIX_simcall_pre(req, 0);
    }
  }
}

int MC_deadlock_check()
{
  int deadlock = FALSE;
  smx_process_t process;
  if(xbt_swag_size(simix_global->process_list)){
    deadlock = TRUE;
    xbt_swag_foreach(process, simix_global->process_list){
      if(process->simcall.call != SIMCALL_NONE
         && MC_request_is_enabled(&process->simcall)){
        deadlock = FALSE;
        break;
      }
    }
  }
  return deadlock;
}

/**
 * \brief Re-executes from the state at position start all the transitions indicated by
 *        a given model-checker stack.
 * \param stack The stack with the transitions to execute.
 * \param start Start index to begin the re-execution.
 */
void MC_replay(xbt_fifo_t stack, int start)
{
  int raw_mem = (mmalloc_get_current_heap() == raw_heap);

  int value, i = 1, count = 1;
  char *req_str;
  smx_simcall_t req = NULL, saved_req = NULL;
  xbt_fifo_item_t item, start_item;
  mc_state_t state;
  smx_process_t process = NULL;

  XBT_DEBUG("**** Begin Replay ****");

  if(start == -1){
    /* Restore the initial state */
    MC_restore_snapshot(initial_state_safety->snapshot);
    /* At the moment of taking the snapshot the raw heap was set, so restoring
     * it will set it back again, we have to unset it to continue  */
    MC_UNSET_RAW_MEM;
  }

  start_item = xbt_fifo_get_last_item(stack);
  if(start != -1){
    while (i != start){
      start_item = xbt_fifo_get_prev_item(start_item);
      i++;
    }
  }

  MC_SET_RAW_MEM;
  xbt_dict_reset(first_enabled_state);
  xbt_swag_foreach(process, simix_global->process_list){
    if(MC_process_is_enabled(process)){
      char *key = bprintf("%lu", process->pid);
      char *data = bprintf("%d", count);
      xbt_dict_set(first_enabled_state, key, data, NULL);
      xbt_free(key);
    }
  }
  MC_UNSET_RAW_MEM;
  

  /* Traverse the stack from the state at position start and re-execute the transitions */
  for (item = start_item;
       item != xbt_fifo_get_first_item(stack);
       item = xbt_fifo_get_prev_item(item)) {

    state = (mc_state_t) xbt_fifo_get_item_content(item);
    saved_req = MC_state_get_executed_request(state, &value);
   
    MC_SET_RAW_MEM;
    char *key = bprintf("%lu", saved_req->issuer->pid);
    xbt_dict_remove(first_enabled_state, key); 
    xbt_free(key);
    MC_UNSET_RAW_MEM;
   
    if(saved_req){
      /* because we got a copy of the executed request, we have to fetch the  
         real one, pointed by the request field of the issuer process */
      req = &saved_req->issuer->simcall;

      /* Debug information */
      if(XBT_LOG_ISENABLED(mc_global, xbt_log_priority_debug)){
        req_str = MC_request_to_string(req, value);
        XBT_DEBUG("Replay: %s (%p)", req_str, state);
        xbt_free(req_str);
      }
    }
    
    SIMIX_simcall_pre(req, value);
    MC_wait_for_requests();

    count++;

    MC_SET_RAW_MEM;
    /* Insert in dict all enabled processes */
    xbt_swag_foreach(process, simix_global->process_list){
      if(MC_process_is_enabled(process) /*&& !MC_state_process_is_done(state, process)*/){
        char *key = bprintf("%lu", process->pid);
        if(xbt_dict_get_or_null(first_enabled_state, key) == NULL){
          char *data = bprintf("%d", count);
          xbt_dict_set(first_enabled_state, key, data, NULL);
        }
        xbt_free(key);
      }
    }
    MC_UNSET_RAW_MEM;
         
    /* Update statistics */
    mc_stats->visited_states++;
    mc_stats->executed_transitions++;

  }

  XBT_DEBUG("**** End Replay ****");

  if(raw_mem)
    MC_SET_RAW_MEM;
  else
    MC_UNSET_RAW_MEM;
  

}

void MC_replay_liveness(xbt_fifo_t stack, int all_stack)
{

  initial_state_liveness->raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  int value;
  char *req_str;
  smx_simcall_t req = NULL, saved_req = NULL;
  xbt_fifo_item_t item;
  mc_state_t state;
  mc_pair_t pair;
  int depth = 1;

  XBT_DEBUG("**** Begin Replay ****");

  /* Restore the initial state */
  MC_restore_snapshot(initial_state_liveness->snapshot);

  /* At the moment of taking the snapshot the raw heap was set, so restoring
   * it will set it back again, we have to unset it to continue  */
  if(!initial_state_liveness->raw_mem_set)
    MC_UNSET_RAW_MEM;

  if(all_stack){

    item = xbt_fifo_get_last_item(stack);

    while(depth <= xbt_fifo_size(stack)){

      pair = (mc_pair_t) xbt_fifo_get_item_content(item);
      state = (mc_state_t) pair->graph_state;

      if(pair->requests > 0){
   
        saved_req = MC_state_get_executed_request(state, &value);
        //XBT_DEBUG("SavedReq->call %u", saved_req->call);
      
        if(saved_req != NULL){
          /* because we got a copy of the executed request, we have to fetch the  
             real one, pointed by the request field of the issuer process */
          req = &saved_req->issuer->simcall;
          //XBT_DEBUG("Req->call %u", req->call);
  
          /* Debug information */
          if(XBT_LOG_ISENABLED(mc_global, xbt_log_priority_debug)){
            req_str = MC_request_to_string(req, value);
            XBT_DEBUG("Replay (depth = %d) : %s (%p)", depth, req_str, state);
            xbt_free(req_str);
          }
  
        }
 
        SIMIX_simcall_pre(req, value);
        MC_wait_for_requests();
      }

      depth++;
    
      /* Update statistics */
      mc_stats->visited_pairs++;
      mc_stats->executed_transitions++;

      item = xbt_fifo_get_prev_item(item);
    }

  }else{

    /* Traverse the stack from the initial state and re-execute the transitions */
    for (item = xbt_fifo_get_last_item(stack);
         item != xbt_fifo_get_first_item(stack);
         item = xbt_fifo_get_prev_item(item)) {

      pair = (mc_pair_t) xbt_fifo_get_item_content(item);
      state = (mc_state_t) pair->graph_state;

      if(pair->requests > 0){
   
        saved_req = MC_state_get_executed_request(state, &value);
        //XBT_DEBUG("SavedReq->call %u", saved_req->call);
      
        if(saved_req != NULL){
          /* because we got a copy of the executed request, we have to fetch the  
             real one, pointed by the request field of the issuer process */
          req = &saved_req->issuer->simcall;
          //XBT_DEBUG("Req->call %u", req->call);
  
          /* Debug information */
          if(XBT_LOG_ISENABLED(mc_global, xbt_log_priority_debug)){
            req_str = MC_request_to_string(req, value);
            XBT_DEBUG("Replay (depth = %d) : %s (%p)", depth, req_str, state);
            xbt_free(req_str);
          }
  
        }
 
        SIMIX_simcall_pre(req, value);
        MC_wait_for_requests();
      }

      depth++;
    
      /* Update statistics */
      mc_stats->visited_pairs++;
      mc_stats->executed_transitions++;
    }
  }  

  XBT_DEBUG("**** End Replay ****");

  if(initial_state_liveness->raw_mem_set)
    MC_SET_RAW_MEM;
  else
    MC_UNSET_RAW_MEM;
  
}

/**
 * \brief Dumps the contents of a model-checker's stack and shows the actual
 *        execution trace
 * \param stack The stack to dump
 */
void MC_dump_stack_safety(xbt_fifo_t stack)
{
  
  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_show_stack_safety(stack);

  if(!_sg_mc_checkpoint){

    mc_state_t state;

    MC_SET_RAW_MEM;
    while ((state = (mc_state_t) xbt_fifo_pop(stack)) != NULL)
      MC_state_delete(state);
    MC_UNSET_RAW_MEM;

  }

  if(raw_mem_set)
    MC_SET_RAW_MEM;
  else
    MC_UNSET_RAW_MEM;
  
}


void MC_show_stack_safety(xbt_fifo_t stack)
{

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;

  int value;
  mc_state_t state;
  xbt_fifo_item_t item;
  smx_simcall_t req;
  char *req_str = NULL;
  
  for (item = xbt_fifo_get_last_item(stack);
       (item ? (state = (mc_state_t) (xbt_fifo_get_item_content(item)))
        : (NULL)); item = xbt_fifo_get_prev_item(item)) {
    req = MC_state_get_executed_request(state, &value);
    if(req){
      req_str = MC_request_to_string(req, value);
      XBT_INFO("%s", req_str);
      xbt_free(req_str);
    }
  }

  if(!raw_mem_set)
    MC_UNSET_RAW_MEM;
}

void MC_show_deadlock(smx_simcall_t req)
{
  /*char *req_str = NULL;*/
  XBT_INFO("**************************");
  XBT_INFO("*** DEAD-LOCK DETECTED ***");
  XBT_INFO("**************************");
  XBT_INFO("Locked request:");
  /*req_str = MC_request_to_string(req);
    XBT_INFO("%s", req_str);
    xbt_free(req_str);*/
  XBT_INFO("Counter-example execution trace:");
  MC_dump_stack_safety(mc_stack_safety);
  MC_print_statistics(mc_stats);
}


void MC_show_stack_liveness(xbt_fifo_t stack){
  int value;
  mc_pair_t pair;
  xbt_fifo_item_t item;
  smx_simcall_t req;
  char *req_str = NULL;
  
  for (item = xbt_fifo_get_last_item(stack);
       (item ? (pair = (mc_pair_t) (xbt_fifo_get_item_content(item)))
        : (NULL)); item = xbt_fifo_get_prev_item(item)) {
    req = MC_state_get_executed_request(pair->graph_state, &value);
    if(req){
      if(pair->requests>0){
        req_str = MC_request_to_string(req, value);
        XBT_INFO("%s", req_str);
        xbt_free(req_str);
      }else{
        XBT_INFO("End of system requests but evolution in Büchi automaton");
      }
    }
  }
}

void MC_dump_stack_liveness(xbt_fifo_t stack){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  mc_pair_t pair;

  MC_SET_RAW_MEM;
  while ((pair = (mc_pair_t) xbt_fifo_pop(stack)) != NULL)
    MC_pair_delete(pair);
  MC_UNSET_RAW_MEM;

  if(raw_mem_set)
    MC_SET_RAW_MEM;

}


void MC_print_statistics(mc_stats_t stats)
{
  if(stats->expanded_pairs == 0){
    XBT_INFO("Expanded states = %lu", stats->expanded_states);
    XBT_INFO("Visited states = %lu", stats->visited_states);
  }else{
    XBT_INFO("Expanded pairs = %lu", stats->expanded_pairs);
    XBT_INFO("Visited pairs = %lu", stats->visited_pairs);
  }
  XBT_INFO("Executed transitions = %lu", stats->executed_transitions);
  MC_SET_RAW_MEM;
  if((_sg_mc_dot_output_file != NULL) && (_sg_mc_dot_output_file[0]!='\0')){
    fprintf(dot_output, "}\n");
    fclose(dot_output);
  }
  MC_UNSET_RAW_MEM;
}

void MC_assert(int prop)
{
  if (MC_is_active() && !prop){
    XBT_INFO("**************************");
    XBT_INFO("*** PROPERTY NOT VALID ***");
    XBT_INFO("**************************");
    XBT_INFO("Counter-example execution trace:");
    MC_dump_stack_safety(mc_stack_safety);
    MC_print_statistics(mc_stats);
    xbt_abort();
  }
}

static void MC_assert_pair(int prop){
  if (MC_is_active() && !prop) {
    XBT_INFO("**************************");
    XBT_INFO("*** PROPERTY NOT VALID ***");
    XBT_INFO("**************************");
    //XBT_INFO("Counter-example execution trace:");
    MC_show_stack_liveness(mc_stack_liveness);
    //MC_dump_snapshot_stack(mc_snapshot_stack);
    MC_print_statistics(mc_stats);
    xbt_abort();
  }
}

void MC_process_clock_add(smx_process_t process, double amount)
{
  mc_time[process->pid] += amount;
}

double MC_process_clock_get(smx_process_t process)
{
  if(mc_time){
    if(process != NULL)
      return mc_time[process->pid];
    else 
      return -1;
  }else{
    return 0;
  }
}

void MC_automaton_load(const char *file){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;

  if (_mc_property_automaton == NULL)
    _mc_property_automaton = xbt_automaton_new();
  
  xbt_automaton_load(_mc_property_automaton,file);

  MC_UNSET_RAW_MEM;

  if(raw_mem_set)
    MC_SET_RAW_MEM;

}

void MC_automaton_new_propositional_symbol(const char* id, void* fct) {

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;

  if (_mc_property_automaton == NULL)
    _mc_property_automaton = xbt_automaton_new();

  xbt_automaton_propositional_symbol_new(_mc_property_automaton,id,fct);

  MC_UNSET_RAW_MEM;

  if(raw_mem_set)
    MC_SET_RAW_MEM;
  
}

/************ MC_ignore ***********/ 

void heap_ignore_region_free(mc_heap_ignore_region_t r){
  xbt_free(r);
}

void heap_ignore_region_free_voidp(void *r){
  heap_ignore_region_free((mc_heap_ignore_region_t) * (void **) r);
}

void MC_ignore_heap(void *address, size_t size){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;

  mc_heap_ignore_region_t region = NULL;
  region = xbt_new0(s_mc_heap_ignore_region_t, 1);
  region->address = address;
  region->size = size;
  
  region->block = ((char*)address - (char*)((xbt_mheap_t)std_heap)->heapbase) / BLOCKSIZE + 1;
  
  if(((xbt_mheap_t)std_heap)->heapinfo[region->block].type == 0){
    region->fragment = -1;
    ((xbt_mheap_t)std_heap)->heapinfo[region->block].busy_block.ignore++;
  }else{
    region->fragment = ((uintptr_t) (ADDR2UINT (address) % (BLOCKSIZE))) >> ((xbt_mheap_t)std_heap)->heapinfo[region->block].type;
    ((xbt_mheap_t)std_heap)->heapinfo[region->block].busy_frag.ignore[region->fragment]++;
  }
  
  if(mc_heap_comparison_ignore == NULL){
    mc_heap_comparison_ignore = xbt_dynar_new(sizeof(mc_heap_ignore_region_t), heap_ignore_region_free_voidp);
    xbt_dynar_push(mc_heap_comparison_ignore, &region);
    if(!raw_mem_set)
      MC_UNSET_RAW_MEM;
    return;
  }

  unsigned int cursor = 0;
  mc_heap_ignore_region_t current_region = NULL;
  int start = 0;
  int end = xbt_dynar_length(mc_heap_comparison_ignore) - 1;
  
  while(start <= end){
    cursor = (start + end) / 2;
    current_region = (mc_heap_ignore_region_t)xbt_dynar_get_as(mc_heap_comparison_ignore, cursor, mc_heap_ignore_region_t);
    if(current_region->address == address){
      heap_ignore_region_free(region);
      if(!raw_mem_set)
        MC_UNSET_RAW_MEM;
      return;
    }
    if(current_region->address < address)
      start = cursor + 1;
    if(current_region->address > address)
      end = cursor - 1;   
  }

  if(current_region->address < address)
    xbt_dynar_insert_at(mc_heap_comparison_ignore, cursor + 1, &region);
  else
    xbt_dynar_insert_at(mc_heap_comparison_ignore, cursor, &region);

  MC_UNSET_RAW_MEM;

  if(raw_mem_set)
    MC_SET_RAW_MEM;
}

void MC_remove_ignore_heap(void *address, size_t size){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;

  unsigned int cursor = 0;
  int start = 0;
  int end = xbt_dynar_length(mc_heap_comparison_ignore) - 1;
  mc_heap_ignore_region_t region;
  int ignore_found = 0;

  while(start <= end){
    cursor = (start + end) / 2;
    region = (mc_heap_ignore_region_t)xbt_dynar_get_as(mc_heap_comparison_ignore, cursor, mc_heap_ignore_region_t);
    if(region->address == address){
      ignore_found = 1;
      break;
    }
    if(region->address < address)
      start = cursor + 1;
    if(region->address > address){
      if((char * )region->address <= ((char *)address + size)){
        ignore_found = 1;
        break;
      }else
        end = cursor - 1;   
    }
  }
  
  if(ignore_found == 1){
    xbt_dynar_remove_at(mc_heap_comparison_ignore, cursor, NULL);
    MC_remove_ignore_heap(address, size);
  }

  MC_UNSET_RAW_MEM;
  
  if(raw_mem_set)
    MC_SET_RAW_MEM;

}

void data_bss_ignore_variable_free(mc_data_bss_ignore_variable_t v){
  xbt_free(v);
}

void data_bss_ignore_variable_free_voidp(void *v){
  data_bss_ignore_variable_free((mc_data_bss_ignore_variable_t) * (void **) v);
}

void MC_ignore_data_bss(void *address, size_t size){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;
  
  if(mc_data_bss_comparison_ignore == NULL)
    mc_data_bss_comparison_ignore = xbt_dynar_new(sizeof(mc_data_bss_ignore_variable_t), data_bss_ignore_variable_free_voidp);

  mc_data_bss_ignore_variable_t var = NULL;
  var = xbt_new0(s_mc_data_bss_ignore_variable_t, 1);
  var->address = address;
  var->size = size;

  if(xbt_dynar_is_empty(mc_data_bss_comparison_ignore)){

    xbt_dynar_insert_at(mc_data_bss_comparison_ignore, 0, &var);

  }else{
    
    unsigned int cursor = 0;
    int start = 0;
    int end = xbt_dynar_length(mc_data_bss_comparison_ignore) - 1;
    mc_data_bss_ignore_variable_t current_var = NULL;

    while(start <= end){
      cursor = (start + end) / 2;
      current_var = (mc_data_bss_ignore_variable_t)xbt_dynar_get_as(mc_data_bss_comparison_ignore, cursor, mc_data_bss_ignore_variable_t);
      if(current_var->address == address){
        data_bss_ignore_variable_free(var);
        MC_UNSET_RAW_MEM;
        if(raw_mem_set)
          MC_SET_RAW_MEM;
        return;
      }
      if(current_var->address < address)
        start = cursor + 1;
      if(current_var->address > address)
        end = cursor - 1;
    }

    if(current_var->address < address)
      xbt_dynar_insert_at(mc_data_bss_comparison_ignore, cursor + 1, &var);
    else
      xbt_dynar_insert_at(mc_data_bss_comparison_ignore, cursor, &var);

  }

  /* Remove variable from mc_global_variables */

  if(mc_global_variables != NULL){

    unsigned int cursor = 0;
    int start = 0;
    int end = xbt_dynar_length(mc_global_variables) - 1;
    global_variable_t current_var;
    int var_found;

    while(start <= end){
      cursor = (start + end) / 2;
      current_var = (global_variable_t)xbt_dynar_get_as(mc_global_variables, cursor, global_variable_t);
      if(current_var->address == var->address){
        var_found = 1;
        break;
      }
      if(current_var->address < address)
        start = cursor + 1;
      if(current_var->address > address)
        end = cursor - 1;
    }

    if(var_found)
      xbt_dynar_remove_at(mc_global_variables, cursor, NULL);
    
  }

  MC_UNSET_RAW_MEM;

  if(raw_mem_set)
    MC_SET_RAW_MEM;
}

static size_t data_bss_ignore_size(void *address){
  unsigned int cursor = 0;
  int start = 0;
  int end = xbt_dynar_length(mc_data_bss_comparison_ignore) - 1;
  mc_data_bss_ignore_variable_t var;

  while(start <= end){
    cursor = (start + end) / 2;
    var = (mc_data_bss_ignore_variable_t)xbt_dynar_get_as(mc_data_bss_comparison_ignore, cursor, mc_data_bss_ignore_variable_t);
    if(var->address == address)
      return var->size;
    if(var->address < address){
      if((void *)((char *)var->address + var->size) > address)
        return (char *)var->address + var->size - (char*)address;
      else
        start = cursor + 1;
    }
    if(var->address > address)
      end = cursor - 1;   
  }

  return 0;
}

void stack_ignore_variable_free(mc_stack_ignore_variable_t v){
  xbt_free(v->var_name);
  xbt_free(v->frame);
  xbt_free(v);
}

void stack_ignore_variable_free_voidp(void *v){
  stack_ignore_variable_free((mc_stack_ignore_variable_t) * (void **) v);
}

void MC_ignore_stack(const char *var_name, const char *frame_name){
  
  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;

  if(mc_stack_comparison_ignore == NULL)
    mc_stack_comparison_ignore = xbt_dynar_new(sizeof(mc_stack_ignore_variable_t), stack_ignore_variable_free_voidp);
  
  mc_stack_ignore_variable_t var = NULL;
  var = xbt_new0(s_mc_stack_ignore_variable_t, 1);
  var->var_name = strdup(var_name);
  var->frame = strdup(frame_name);
  
  if(xbt_dynar_is_empty(mc_stack_comparison_ignore)){

    xbt_dynar_insert_at(mc_stack_comparison_ignore, 0, &var);

  }else{
    
    unsigned int cursor = 0;
    int start = 0;
    int end = xbt_dynar_length(mc_stack_comparison_ignore) - 1;
    mc_stack_ignore_variable_t current_var = NULL;

    while(start <= end){
      cursor = (start + end) / 2;
      current_var = (mc_stack_ignore_variable_t)xbt_dynar_get_as(mc_stack_comparison_ignore, cursor, mc_stack_ignore_variable_t);
      if(strcmp(current_var->frame, frame_name) == 0){
        if(strcmp(current_var->var_name, var_name) == 0){
          stack_ignore_variable_free(var);
          MC_UNSET_RAW_MEM;
          if(raw_mem_set)
            MC_SET_RAW_MEM;
          return;
        }
        if(strcmp(current_var->var_name, var_name) < 0)
          start = cursor + 1;
        if(strcmp(current_var->var_name, var_name) > 0)
          end = cursor - 1;
      }
      if(strcmp(current_var->frame, frame_name) < 0)
        start = cursor + 1;
      if(strcmp(current_var->frame, frame_name) > 0)
        end = cursor - 1;
    }

    if(strcmp(current_var->frame, frame_name) < 0)
      xbt_dynar_insert_at(mc_stack_comparison_ignore, cursor + 1, &var);
    else
      xbt_dynar_insert_at(mc_stack_comparison_ignore, cursor, &var);

  }

 /* Remove variable from mc_local_variables */

  if(mc_local_variables != NULL){

    if(strcmp(frame_name, "*") != 0){
      dw_frame_t frame = xbt_dict_get_or_null(mc_local_variables, frame_name);
      if(frame != NULL)
        xbt_dict_remove(frame->variables, var_name);
    }

  }

  MC_UNSET_RAW_MEM;
  
  if(raw_mem_set)
    MC_SET_RAW_MEM;

}

void MC_new_stack_area(void *stack, char *name, void* context, size_t size){

  int raw_mem_set = (mmalloc_get_current_heap() == raw_heap);

  MC_SET_RAW_MEM;
  if(stacks_areas == NULL)
    stacks_areas = xbt_dynar_new(sizeof(stack_region_t), NULL);
  
  stack_region_t region = NULL;
  region = xbt_new0(s_stack_region_t, 1);
  region->address = stack;
  region->process_name = strdup(name);
  region->context = context;
  region->size = size;
  region->block = ((char*)stack - (char*)((xbt_mheap_t)std_heap)->heapbase) / BLOCKSIZE + 1;
  xbt_dynar_push(stacks_areas, &region);
  
  MC_UNSET_RAW_MEM;

  if(raw_mem_set)
    MC_SET_RAW_MEM;
}

/************ DWARF ***********/

xbt_dict_t MC_get_location_list(const char *elf_file){

  char *command = bprintf("objdump -Wo %s", elf_file);

  FILE *fp = popen(command, "r");

  if(fp == NULL){
    perror("popen for objdump failed");
    xbt_abort();
  }

  int debug = 0; /*Detect if the program has been compiled with -g */

  xbt_dict_t location_list = xbt_dict_new_homogeneous(NULL);
  char *line = NULL, *loc_expr = NULL;
  ssize_t read;
  size_t n = 0;
  int cursor_remove;
  xbt_dynar_t split = NULL;

  while ((read = xbt_getline(&line, &n, fp)) != -1) {

    /* Wipeout the new line character */
    line[read - 1] = '\0';

    xbt_str_trim(line, NULL);
    
    if(n == 0)
      continue;

    if(strlen(line) == 0)
      continue;

    if(debug == 0){

      if(strncmp(line, elf_file, strlen(elf_file)) == 0)
        continue;
      
      if(strncmp(line, "Contents", 8) == 0)
        continue;

      if(strncmp(line, "Offset", 6) == 0){
        debug = 1;
        continue;
      }
    }

    if(debug == 0){
      XBT_INFO("Your program must be compiled with -g");
      xbt_abort();
    }

    xbt_dynar_t loclist = xbt_dynar_new(sizeof(dw_location_entry_t), NULL);

    xbt_str_strip_spaces(line);
    split = xbt_str_split(line, " ");

    while(read != -1 && strcmp("<End", (char *)xbt_dynar_get_as(split, 1, char *)) != 0){
      
      dw_location_entry_t new_entry = xbt_new0(s_dw_location_entry_t, 1);
      new_entry->lowpc = strtoul((char *)xbt_dynar_get_as(split, 1, char *), NULL, 16);
      new_entry->highpc = strtoul((char *)xbt_dynar_get_as(split, 2, char *), NULL, 16);
      
      cursor_remove =0;
      while(cursor_remove < 3){
        xbt_dynar_remove_at(split, 0, NULL);
        cursor_remove++;
      }

      loc_expr = xbt_str_join(split, " ");
      xbt_str_ltrim(loc_expr, "(");
      xbt_str_rtrim(loc_expr, ")");
      new_entry->location = get_location(NULL, loc_expr);

      xbt_dynar_push(loclist, &new_entry);

      xbt_dynar_free(&split);
      free(loc_expr);

      read = xbt_getline(&line, &n, fp);
      if(read != -1){
        line[read - 1] = '\0';
        xbt_str_strip_spaces(line);
        split = xbt_str_split(line, " ");
      }

    }


    char *key = bprintf("%d", (int)strtoul((char *)xbt_dynar_get_as(split, 0, char *), NULL, 16));
    xbt_dict_set(location_list, key, loclist, NULL);
    xbt_free(key);
    
    xbt_dynar_free(&split);

  }

  xbt_free(line);
  xbt_free(command);
  pclose(fp);

  return location_list;
}

static dw_frame_t get_frame_by_offset(xbt_dict_t all_variables, unsigned long int offset){

  xbt_dict_cursor_t cursor = NULL;
  char *name;
  dw_frame_t res;

  xbt_dict_foreach(all_variables, cursor, name, res) {
    if(offset >= res->start && offset < res->end){
      xbt_dict_cursor_free(&cursor);
      return res;
    }
  }

  xbt_dict_cursor_free(&cursor);
  return NULL;
  
}

void MC_get_local_variables(const char *elf_file, xbt_dict_t location_list, xbt_dict_t *all_variables){

  char *command = bprintf("objdump -Wi %s", elf_file);
  
  FILE *fp = popen(command, "r");

  if(fp == NULL)
    perror("popen for objdump failed");

  char *line = NULL, *origin, *abstract_origin, *current_frame = NULL;
  ssize_t read =0;
  size_t n = 0;
  int valid_variable = 1;
  char *node_type = NULL, *location_type = NULL, *variable_name = NULL, *loc_expr = NULL;
  xbt_dynar_t split = NULL, split2 = NULL;

  xbt_dict_t variables_origin = xbt_dict_new_homogeneous(NULL);
  xbt_dict_t subprograms_origin = xbt_dict_new_homogeneous(NULL);
  char *subprogram_name = NULL, *subprogram_start = NULL, *subprogram_end = NULL;
  int new_frame = 0, new_variable = 0;
  dw_frame_t variable_frame, subroutine_frame = NULL;

  read = xbt_getline(&line, &n, fp);

  while (read != -1) {

    if(n == 0){
      read = xbt_getline(&line, &n, fp);
      continue;
    }
 
    /* Wipeout the new line character */
    line[read - 1] = '\0';
   
    if(strlen(line) == 0){
      read = xbt_getline(&line, &n, fp);
      continue;
    }

    xbt_str_ltrim(line, NULL);
    xbt_str_strip_spaces(line);
    
    if(line[0] != '<'){
      read = xbt_getline(&line, &n, fp);
      continue;
    }
    
    xbt_dynar_free(&split);
    split = xbt_str_split(line, " ");

    /* Get node type */
    node_type = xbt_dynar_get_as(split, xbt_dynar_length(split) - 1, char *);

    if(strcmp(node_type, "(DW_TAG_subprogram)") == 0){ /* New frame */

      dw_frame_t frame = NULL;

      strtok(xbt_dynar_get_as(split, 0, char *), "<");
      subprogram_start = strdup(strtok(NULL, "<"));
      xbt_str_rtrim(subprogram_start, ">:");

      read = xbt_getline(&line, &n, fp);
   
      while(read != -1){

        if(n == 0){
          read = xbt_getline(&line, &n, fp);
          continue;
        }

        /* Wipeout the new line character */
        line[read - 1] = '\0';
        
        if(strlen(line) == 0){
          read = xbt_getline(&line, &n, fp);
          continue;
        }
      
        xbt_dynar_free(&split);
        xbt_str_rtrim(line, NULL);
        xbt_str_strip_spaces(line);
        split = xbt_str_split(line, " ");
          
        node_type = xbt_dynar_get_as(split, 1, char *);

        if(strncmp(node_type, "DW_AT_", 6) != 0)
          break;

        if(strcmp(node_type, "DW_AT_sibling") == 0){

          subprogram_end = strdup(xbt_dynar_get_as(split, 3, char*));
          xbt_str_ltrim(subprogram_end, "<0x");
          xbt_str_rtrim(subprogram_end, ">");
          
        }else if(strcmp(node_type, "DW_AT_abstract_origin:") == 0){ /* Frame already in dict */
          
          new_frame = 0;
          abstract_origin = strdup(xbt_dynar_get_as(split, 2, char*));
          xbt_str_ltrim(abstract_origin, "<0x");
          xbt_str_rtrim(abstract_origin, ">");
          subprogram_name = (char *)xbt_dict_get_or_null(subprograms_origin, abstract_origin);
          frame = xbt_dict_get_or_null(*all_variables, subprogram_name); 
          xbt_free(abstract_origin);

        }else if(strcmp(node_type, "DW_AT_name") == 0){

          new_frame = 1;
          xbt_free(current_frame);
          frame = xbt_new0(s_dw_frame_t, 1);
          frame->name = strdup(xbt_dynar_get_as(split, xbt_dynar_length(split) - 1, char *)); 
          frame->variables = xbt_dict_new_homogeneous(NULL);
          frame->frame_base = xbt_new0(s_dw_location_t, 1); 
          current_frame = strdup(frame->name);

          xbt_dict_set(subprograms_origin, subprogram_start, frame->name, NULL);
        
        }else if(strcmp(node_type, "DW_AT_frame_base") == 0){

          location_type = xbt_dynar_get_as(split, xbt_dynar_length(split) - 1, char *);

          if(strcmp(location_type, "list)") == 0){ /* Search location in location list */

            frame->frame_base = get_location(location_list, xbt_dynar_get_as(split, 3, char *));
             
          }else{
                
            xbt_str_strip_spaces(line);
            split2 = xbt_str_split(line, "(");
            xbt_dynar_remove_at(split2, 0, NULL);
            loc_expr = xbt_str_join(split2, " ");
            xbt_str_rtrim(loc_expr, ")");
            frame->frame_base = get_location(NULL, loc_expr);
            xbt_dynar_free(&split2);
            xbt_free(loc_expr);

          }
 
        }else if(strcmp(node_type, "DW_AT_low_pc") == 0){
          
          if(frame != NULL)
            frame->low_pc = (void *)strtoul(xbt_dynar_get_as(split, 3, char *), NULL, 16);

        }else if(strcmp(node_type, "DW_AT_high_pc") == 0){

          if(frame != NULL)
            frame->high_pc = (void *)strtoul(xbt_dynar_get_as(split, 3, char *), NULL, 16);

        }else if(strcmp(node_type, "DW_AT_MIPS_linkage_name:") == 0){

          xbt_free(frame->name);
          xbt_free(current_frame);
          frame->name = strdup(xbt_dynar_get_as(split, xbt_dynar_length(split) - 1, char *));   
          current_frame = strdup(frame->name);
          xbt_dict_set(subprograms_origin, subprogram_start, frame->name, NULL);

        }

        read = xbt_getline(&line, &n, fp);

      }
 
      if(new_frame == 1){
        frame->start = strtoul(subprogram_start, NULL, 16);
        if(subprogram_end != NULL)
          frame->end = strtoul(subprogram_end, NULL, 16);
        xbt_dict_set(*all_variables, frame->name, frame, NULL);
      }

      xbt_free(subprogram_start);
      xbt_free(subprogram_end);
      subprogram_end = NULL;
        

    }else if(strcmp(node_type, "(DW_TAG_variable)") == 0){ /* New variable */

      dw_local_variable_t var = NULL;
      
      strtok(xbt_dynar_get_as(split, 0, char *), "<");
      origin = strdup(strtok(NULL, "<"));
      xbt_str_rtrim(origin, ">:");
      
      read = xbt_getline(&line, &n, fp);
      
      while(read != -1){

        if(n == 0){
          read = xbt_getline(&line, &n, fp);
          continue;
        }

        /* Wipeout the new line character */
        line[read - 1] = '\0'; 

        if(strlen(line) == 0){
          read = xbt_getline(&line, &n, fp);
          continue;
        }
       
        xbt_dynar_free(&split);
        xbt_str_rtrim(line, NULL);
        xbt_str_strip_spaces(line);
        split = xbt_str_split(line, " ");
  
        node_type = xbt_dynar_get_as(split, 1, char *);

        if(strncmp(node_type, "DW_AT_", 6) != 0)
          break;

        if(strcmp(node_type, "DW_AT_name") == 0){

          new_variable = 1;
          var = xbt_new0(s_dw_local_variable_t, 1);
          var->name = strdup(xbt_dynar_get_as(split, xbt_dynar_length(split) - 1, char *));

          xbt_dict_set(variables_origin, origin, var->name, NULL);
         
        }else if(strcmp(node_type, "DW_AT_abstract_origin:") == 0){

          new_variable = 0;
          abstract_origin = xbt_dynar_get_as(split, 2, char *);
          xbt_str_ltrim(abstract_origin, "<0x");
          xbt_str_rtrim(abstract_origin, ">");
          
          variable_name = (char *)xbt_dict_get_or_null(variables_origin, abstract_origin);
          variable_frame = get_frame_by_offset(*all_variables, strtoul(abstract_origin, NULL, 16));
          var = xbt_dict_get_or_null(variable_frame->variables, variable_name);   

        }else if(strcmp(node_type, "DW_AT_location") == 0){

          if(valid_variable == 1 && var != NULL){

            var->location = xbt_new0(s_dw_location_t, 1);

            location_type = xbt_dynar_get_as(split, xbt_dynar_length(split) - 1, char *);

            if(strcmp(location_type, "list)") == 0){ /* Search location in location list */

              var->location = get_location(location_list, xbt_dynar_get_as(split, 3, char *));
             
            }else{
                
              xbt_str_strip_spaces(line);
              split2 = xbt_str_split(line, "(");
              xbt_dynar_remove_at(split2, 0, NULL);
              loc_expr = xbt_str_join(split2, " ");
              xbt_str_rtrim(loc_expr, ")");
              var->location = get_location(NULL, loc_expr);
              xbt_dynar_free(&split2);
              xbt_free(loc_expr);

            }

          }
           
        }else if(strcmp(node_type, "DW_AT_external") == 0){

          valid_variable = 0;
        
        }

        read = xbt_getline(&line, &n, fp);
 
      }

      if(new_variable == 1 && valid_variable == 1){
        
        variable_frame = xbt_dict_get_or_null(*all_variables, current_frame);
        xbt_dict_set(variable_frame->variables, var->name, var, NULL);
      }

      valid_variable = 1;
      new_variable = 0;

    }else if(strcmp(node_type, "(DW_TAG_inlined_subroutine)") == 0){

      strtok(xbt_dynar_get_as(split, 0, char *), "<");
      origin = strdup(strtok(NULL, "<"));
      xbt_str_rtrim(origin, ">:");

      read = xbt_getline(&line, &n, fp);

      while(read != -1){

        /* Wipeout the new line character */
        line[read - 1] = '\0'; 

        if(n == 0){
          read = xbt_getline(&line, &n, fp);
          continue;
        }

        if(strlen(line) == 0){
          read = xbt_getline(&line, &n, fp);
          continue;
        }

        xbt_dynar_free(&split);
        xbt_str_rtrim(line, NULL);
        xbt_str_strip_spaces(line);
        split = xbt_str_split(line, " ");
        
        if(strncmp(xbt_dynar_get_as(split, 1, char *), "DW_AT_", 6) != 0)
          break;
          
        node_type = xbt_dynar_get_as(split, 1, char *);

        if(strcmp(node_type, "DW_AT_abstract_origin:") == 0){

          origin = xbt_dynar_get_as(split, 2, char *);
          xbt_str_ltrim(origin, "<0x");
          xbt_str_rtrim(origin, ">");
          
          subprogram_name = (char *)xbt_dict_get_or_null(subprograms_origin, origin);
          subroutine_frame = xbt_dict_get_or_null(*all_variables, subprogram_name);
        
        }else if(strcmp(node_type, "DW_AT_low_pc") == 0){

          subroutine_frame->low_pc = (void *)strtoul(xbt_dynar_get_as(split, 3, char *), NULL, 16);

        }else if(strcmp(node_type, "DW_AT_high_pc") == 0){

          subroutine_frame->high_pc = (void *)strtoul(xbt_dynar_get_as(split, 3, char *), NULL, 16);
        }

        read = xbt_getline(&line, &n, fp);
      
      }

    }else{

      read = xbt_getline(&line, &n, fp);

    }

  }
  
  xbt_dynar_free(&split);
  xbt_free(line);
  xbt_free(command);
  pclose(fp);
  
}

static dw_location_t get_location(xbt_dict_t location_list, char *expr){

  dw_location_t loc = xbt_new0(s_dw_location_t, 1);

  if(location_list != NULL){
    
    char *key = bprintf("%d", (int)strtoul(expr, NULL, 16));
    loc->type = e_dw_loclist;
    loc->location.loclist =  (xbt_dynar_t)xbt_dict_get_or_null(location_list, key);
    if(loc->location.loclist == NULL)
      XBT_INFO("Key not found in loclist");
    xbt_free(key);
    return loc;

  }else{

    int cursor = 0;
    char *tok = NULL, *tok2 = NULL; 
    
    xbt_dynar_t tokens1 = xbt_str_split(expr, ";");
    xbt_dynar_t tokens2;

    loc->type = e_dw_compose;
    loc->location.compose = xbt_dynar_new(sizeof(dw_location_t), NULL);

    while(cursor < xbt_dynar_length(tokens1)){

      tok = xbt_dynar_get_as(tokens1, cursor, char*);
      tokens2 = xbt_str_split(tok, " ");
      tok2 = xbt_dynar_get_as(tokens2, 0, char*);
      
      if(strncmp(tok2, "DW_OP_reg", 9) == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_register;
        new_element->location.reg = atoi(strtok(tok2, "DW_OP_reg"));
        xbt_dynar_push(loc->location.compose, &new_element);     
      }else if(strcmp(tok2, "DW_OP_fbreg:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_fbregister_op;
        new_element->location.fbreg_op = atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strncmp(tok2, "DW_OP_breg", 10) == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_bregister_op;
        new_element->location.breg_op.reg = atoi(strtok(tok2, "DW_OP_breg"));
        new_element->location.breg_op.offset = atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strncmp(tok2, "DW_OP_lit", 9) == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_lit;
        new_element->location.lit = atoi(strtok(tok2, "DW_OP_lit"));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok2, "DW_OP_piece:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_piece;
        new_element->location.piece = atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*));
        /*if(strlen(xbt_dynar_get_as(tokens2, 1, char*)) > 1)
          new_element->location.piece = atoi(xbt_dynar_get_as(tokens2, 1, char*));
        else
        new_element->location.piece = xbt_dynar_get_as(tokens2, 1, char*)[0] - '0';*/
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok2, "DW_OP_plus_uconst:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_plus_uconst;
        new_element->location.plus_uconst = atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char *));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok, "DW_OP_abs") == 0 || 
               strcmp(tok, "DW_OP_and") == 0 ||
               strcmp(tok, "DW_OP_div") == 0 ||
               strcmp(tok, "DW_OP_minus") == 0 ||
               strcmp(tok, "DW_OP_mod") == 0 ||
               strcmp(tok, "DW_OP_mul") == 0 ||
               strcmp(tok, "DW_OP_neg") == 0 ||
               strcmp(tok, "DW_OP_not") == 0 ||
               strcmp(tok, "DW_OP_or") == 0 ||
               strcmp(tok, "DW_OP_plus") == 0){               
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_arithmetic;
        new_element->location.arithmetic = strdup(strtok(tok2, "DW_OP_"));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok, "DW_OP_stack_value") == 0){
      }else if(strcmp(tok2, "DW_OP_deref_size:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_deref;
        new_element->location.deref_size = (unsigned int short) atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*));
        /*if(strlen(xbt_dynar_get_as(tokens, ++cursor, char*)) > 1)
          new_element->location.deref_size = atoi(xbt_dynar_get_as(tokens, cursor, char*));
        else
        new_element->location.deref_size = xbt_dynar_get_as(tokens, cursor, char*)[0] - '0';*/
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok, "DW_OP_deref") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_deref;
        new_element->location.deref_size = sizeof(void *);
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok2, "DW_OP_constu:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_uconstant;
        new_element->location.uconstant.bytes = 1;
        new_element->location.uconstant.value = (unsigned long int)(atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*)));
        /*if(strlen(xbt_dynar_get_as(tokens, ++cursor, char*)) > 1)
          new_element->location.uconstant.value = (unsigned long int)(atoi(xbt_dynar_get_as(tokens, cursor, char*)));
        else
        new_element->location.uconstant.value = (unsigned long int)(xbt_dynar_get_as(tokens, cursor, char*)[0] - '0');*/
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok2, "DW_OP_consts:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_sconstant;
        new_element->location.sconstant.bytes = 1;
        new_element->location.sconstant.value = (long int)(atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*)));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok2, "DW_OP_const1u:") == 0 ||
               strcmp(tok2, "DW_OP_const2u:") == 0 ||
               strcmp(tok2, "DW_OP_const4u:") == 0 ||
               strcmp(tok2, "DW_OP_const8u:") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_uconstant;
        new_element->location.uconstant.bytes = tok2[11] - '0';
        new_element->location.uconstant.value = (unsigned long int)(atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*)));
        /*if(strlen(xbt_dynar_get_as(tokens, ++cursor, char*)) > 1)
          new_element->location.constant.value = atoi(xbt_dynar_get_as(tokens, cursor, char*));
        else
        new_element->location.constant.value = xbt_dynar_get_as(tokens, cursor, char*)[0] - '0';*/
        xbt_dynar_push(loc->location.compose, &new_element);
      }else if(strcmp(tok, "DW_OP_const1s") == 0 ||
               strcmp(tok, "DW_OP_const2s") == 0 ||
               strcmp(tok, "DW_OP_const4s") == 0 ||
               strcmp(tok, "DW_OP_const8s") == 0){
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_sconstant;
        new_element->location.sconstant.bytes = tok2[11] - '0';
        new_element->location.sconstant.value = (long int)(atoi(xbt_dynar_get_as(tokens2, xbt_dynar_length(tokens2) - 1, char*)));
        xbt_dynar_push(loc->location.compose, &new_element);
      }else{
        dw_location_t new_element = xbt_new0(s_dw_location_t, 1);
        new_element->type = e_dw_unsupported;
        xbt_dynar_push(loc->location.compose, &new_element);
      }

      cursor++;
      xbt_dynar_free(&tokens2);

    }
    
    xbt_dynar_free(&tokens1);

    return loc;
    
  }

}


void print_local_variables(xbt_dict_t list){
  
  dw_location_entry_t entry;
  dw_location_t location_entry;
  unsigned int cursor3 = 0, cursor4 = 0;
  xbt_dict_cursor_t cursor = 0, cursor2 = 0;

  char *frame_name, *variable_name;
  dw_frame_t current_frame;
  dw_local_variable_t current_variable;

  xbt_dict_foreach(list, cursor, frame_name, current_frame){ 
    fprintf(stderr, "Frame name : %s\n", current_frame->name);
    fprintf(stderr, "Location type : %d\n", current_frame->frame_base->type);
    xbt_dict_foreach((xbt_dict_t)current_frame->variables, cursor2, variable_name, current_variable){
      fprintf(stderr, "Name : %s\n", current_variable->name);
      if(current_variable->location == NULL)
        continue;
      fprintf(stderr, "Location type : %d\n", current_variable->location->type);
      switch(current_variable->location->type){
      case e_dw_loclist :
        xbt_dynar_foreach(current_variable->location->location.loclist, cursor3, entry){
          fprintf(stderr, "Lowpc : %lx, Highpc : %lx,", entry->lowpc, entry->highpc);
          switch(entry->location->type){
          case e_dw_register :
            fprintf(stderr, " Location : in register %d\n", entry->location->location.reg);
            break;
          case e_dw_bregister_op:
            fprintf(stderr, " Location : Add %d to the value in register %d\n", entry->location->location.breg_op.offset, entry->location->location.breg_op.reg);
            break;
          case e_dw_lit:
            fprintf(stderr, "Value already kwnown : %d\n", entry->location->location.lit);
            break;
          case e_dw_fbregister_op:
            fprintf(stderr, " Location : %d bytes from logical frame pointer\n", entry->location->location.fbreg_op);
            break;
          case e_dw_compose:
            fprintf(stderr, " Location :\n");
            xbt_dynar_foreach(entry->location->location.compose, cursor4, location_entry){
              switch(location_entry->type){
              case e_dw_register :
                fprintf(stderr, " %d) in register %d\n", cursor4 + 1, location_entry->location.reg);
                break;
              case e_dw_bregister_op:
                fprintf(stderr, " %d) add %d to the value in register %d\n", cursor4 + 1, location_entry->location.breg_op.offset, location_entry->location.breg_op.reg);
                break;
              case e_dw_lit:
                fprintf(stderr, "%d) Value already kwnown : %d\n", cursor4 + 1, location_entry->location.lit);
                break;
              case e_dw_fbregister_op:
                fprintf(stderr, " %d) %d bytes from logical frame pointer\n", cursor4 + 1, location_entry->location.fbreg_op);
                break;
              case e_dw_deref:
                fprintf(stderr, " %d) Pop the stack entry and treats it as an address (size of data %d)\n", cursor4 + 1, location_entry->location.deref_size);
                break;
              case e_dw_arithmetic :
                fprintf(stderr, "%d) arithmetic operation : %s\n", cursor4 + 1, location_entry->location.arithmetic);
                break;
              case e_dw_piece:
                fprintf(stderr, "%d) The %d byte(s) previous value\n", cursor4 + 1, location_entry->location.piece);
                break;
              case e_dw_uconstant :
                fprintf(stderr, "%d) Unsigned constant %lu\n", cursor4 + 1, location_entry->location.uconstant.value);
                break;
              case e_dw_sconstant :
                fprintf(stderr, "%d) Signed constant %lu\n", cursor4 + 1, location_entry->location.sconstant.value);
                break;
              default :
                fprintf(stderr, "%d) Location type not supported\n", cursor4 + 1);
                break;
              }
            }
            break;
          default:
            fprintf(stderr, "Location type not supported\n");
            break;
          }
        }
        break;
      case e_dw_compose:
        cursor4 = 0;
        fprintf(stderr, "Location :\n");
        xbt_dynar_foreach(current_variable->location->location.compose, cursor4, location_entry){
          switch(location_entry->type){
          case e_dw_register :
            fprintf(stderr, " %d) in register %d\n", cursor4 + 1, location_entry->location.reg);
            break;
          case e_dw_bregister_op:
            fprintf(stderr, " %d) add %d to the value in register %d\n", cursor4 + 1, location_entry->location.breg_op.offset, location_entry->location.breg_op.reg);
            break;
          case e_dw_lit:
            fprintf(stderr, "%d) Value already kwnown : %d\n", cursor4 + 1, location_entry->location.lit);
            break;
          case e_dw_fbregister_op:
            fprintf(stderr, " %d) %d bytes from logical frame pointer\n", cursor4 + 1, location_entry->location.fbreg_op);
            break;
          case e_dw_deref:
            fprintf(stderr, " %d) Pop the stack entry and treats it as an address (size of data %d)\n", cursor4 + 1, location_entry->location.deref_size);
            break;
          case e_dw_arithmetic :
            fprintf(stderr, "%d) arithmetic operation : %s\n", cursor4 + 1, location_entry->location.arithmetic);
            break;
          case e_dw_piece:
            fprintf(stderr, "%d) The %d byte(s) previous value\n", cursor4 + 1, location_entry->location.piece);
            break;
          case e_dw_uconstant :
            fprintf(stderr, "%d) Unsigned constant %lu\n", cursor4 + 1, location_entry->location.uconstant.value);
            break;
          case e_dw_sconstant :
            fprintf(stderr, "%d) Signed constant %lu\n", cursor4 + 1, location_entry->location.sconstant.value);
            break;
          default :
            fprintf(stderr, "%d) Location type not supported\n", cursor4 + 1);
            break;
          }
        }
        break;
      default :
        fprintf(stderr, "Location type not supported\n");
        break;
      }
    }
  }

}

static void MC_get_global_variables(char *elf_file){

  FILE *fp;

  char *command = bprintf("objdump -t -j .data -j .bss %s", elf_file);

  fp = popen(command, "r");

  if(fp == NULL){
    perror("popen failed");
    xbt_abort();
  }

  if(mc_global_variables == NULL)
    mc_global_variables = xbt_dynar_new(sizeof(global_variable_t), global_variable_free_voidp);

  char *line = NULL;
  ssize_t read;
  size_t n = 0;

  xbt_dynar_t line_tokens = NULL;
  unsigned long offset;

  int type = strcmp(elf_file, xbt_binary_name); /* 0 = binary, other = libsimgrid */

  while ((read = xbt_getline(&line, &n, fp)) != -1){

    if(n == 0)
      continue;

     /* Wipeout the new line character */
    line[read - 1] = '\0';

    xbt_str_strip_spaces(line);
    xbt_str_ltrim(line, NULL);

    line_tokens = xbt_str_split(line, NULL);

    if(xbt_dynar_length(line_tokens) <= 4 || strcmp(xbt_dynar_get_as(line_tokens, 0, char *), "SYMBOL") == 0)
      continue;

    if((strncmp(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 1, char*), "__gcov", 6) == 0)
       || (strncmp(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 1, char*), "gcov", 4) == 0)
       || (strcmp(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 1, char*), ".data") == 0)
       || (strcmp(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 1, char*), ".bss") == 0)
       || (strncmp(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 1, char*), "stderr", 6) == 0)
       || ((size_t)strtoul(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 2, char*), NULL, 16) == 0))
      continue;

    global_variable_t var = xbt_new0(s_global_variable_t, 1);

    if(type == 0){
      var->address = (void *)strtoul(xbt_dynar_get_as(line_tokens, 0, char*), NULL, 16);
    }else{
      offset = strtoul(xbt_dynar_get_as(line_tokens, 0, char*), NULL, 16);
      var->address = (char *)start_text_libsimgrid+offset;
    }

    var->size = (size_t)strtoul(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 2, char*), NULL, 16);
    var->name = strdup(xbt_dynar_get_as(line_tokens, xbt_dynar_length(line_tokens) - 1, char*));

    if(data_bss_ignore_size(var->address) > 0){
      global_variable_free(var);
    }else{
      if(xbt_dynar_is_empty(mc_global_variables)){
        xbt_dynar_push(mc_global_variables, &var);
      }else{
        unsigned int cursor = 0;
        int start = 0;
        int end = xbt_dynar_length(mc_global_variables) - 1;
        global_variable_t current_var = NULL;
      
        while(start <= end){
          cursor = (start + end) / 2;
          current_var = (global_variable_t)xbt_dynar_get_as(mc_global_variables, cursor, global_variable_t);
          if(current_var->address == var->address)
            break;
          if(current_var->address < var->address)
            start = cursor + 1;
          if(current_var->address > var->address)
            end = cursor - 1;
        }
 
        if(current_var->address < var->address)
          xbt_dynar_insert_at(mc_global_variables, cursor + 1, &var);
        else
          xbt_dynar_insert_at(mc_global_variables, cursor, &var);
      }
    }

    xbt_dynar_free(&line_tokens);

  }

  xbt_free(command);
  xbt_free(line);
  pclose(fp);

}

void global_variable_free(global_variable_t v){
  xbt_free(v->name);
  xbt_free(v);
}

void global_variable_free_voidp(void *v){
  global_variable_free((global_variable_t) * (void **) v);
}
