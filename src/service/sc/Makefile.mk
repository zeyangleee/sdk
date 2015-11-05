C_FILE += $(SROUCE)/service/sc/mod_dipcc_sc.c \
	$(SROUCE)/service/sc/sc_acd.c \
	$(SROUCE)/service/sc/sc_api_process.c \
	$(SROUCE)/service/sc/sc_debug.c \
	$(SROUCE)/service/sc/sc_dialer.c \
	$(SROUCE)/service/sc/sc_ep_bs_adapter.c \
	$(SROUCE)/service/sc/sc_audit.c \
	$(SROUCE)/service/sc/sc_ep_bs_fsm.c \
	$(SROUCE)/service/sc/sc_event_process.c \
	$(SROUCE)/service/sc/sc_httpd.c \
	$(SROUCE)/service/sc/sc_task.c \
	$(SROUCE)/service/sc/sc_lib.c \
	$(SROUCE)/service/sc/sc_tasks_mngt.c \
	$(SROUCE)/service/sc/sc_cw_queue.c \
	$(SROUCE)/service/sc/sc_ep_extensions.c \
	$(SROUCE)/service/sc/sc_data_syn.c \
	$(SROUCE)/service/sc/sc_caller_setting.c \
	$(SROUCE)/service/sc/sc_db.c \
	$(SROUCE)/service/sc/sc_publish.c
	

C_OBJ_FILE += mod_dipcc_sc.$(SUFFIX) \
	sc_api_process.$(SUFFIX) \
	sc_acd.$(SUFFIX) \
	sc_debug.$(SUFFIX) \
	sc_dialer.$(SUFFIX) \
	sc_ep_bs_adapter.$(SUFFIX) \
	sc_audit.$(SUFFIX) \
	sc_ep_bs_fsm.$(SUFFIX) \
	sc_event_process.$(SUFFIX) \
	sc_httpd.$(SUFFIX) \
	sc_task.$(SUFFIX) \
	sc_lib.$(SUFFIX) \
	sc_tasks_mngt.$(SUFFIX) \
	sc_cw_queue.$(SUFFIX) \
	sc_ep_extensions.$(SUFFIX) \
	sc_data_syn.$(SUFFIX) \
	sc_caller_setting.$(SUFFIX) \
	sc_db.$(SUFFIX) \
	sc_publish.$(SUFFIX)

mod_dipcc_sc.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/mod_dipcc_sc.c
	
sc_cw_queue.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_cw_queue.c
	
sc_api_process.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_api_process.c
	
sc_acd.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_acd.c
	
sc_debug.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_debug.c
	
sc_dialer.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_dialer.c
	
sc_ep_bs_adapter.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_ep_bs_adapter.c
	
sc_audit.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_audit.c
	
sc_ep_bs_fsm.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_ep_bs_fsm.c
	
sc_event_process.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_event_process.c

sc_httpd.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_httpd.c
	
sc_task.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_task.c
	
sc_lib.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_lib.c

sc_tasks_mngt.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_tasks_mngt.c

sc_ep_extensions.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_ep_extensions.c
	
sc_data_syn.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_data_syn.c

sc_caller_setting.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_caller_setting.c

sc_db.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_db.c
	
sc_publish.$(SUFFIX) :
	$(C_COMPILE) $(SROUCE)/service/sc/sc_publish.c