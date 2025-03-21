
CFILES = ipt_test_functions.c\
         ipt_test_scenarios.c\
         mvb_test_functions.c\
         mvb_test_scenarios.c\
         uic_test_functions.c\
         uic_test_scenarios.c\
         wtb_test_functions.c\
         wtb_test_scenarios.c\
	 etc_test_scenarios.c\
	 etc_test_functions.c\
	 test_utils.c\
         test.c


all:
	gcc -Wall -O2 $(CFILES) ../output/linux-x86_64-rel/libsdt.a -o test
	gcc -g $(CFILES) ../output/linux-x86_64-rel/libsdt.a -o test_dbg
