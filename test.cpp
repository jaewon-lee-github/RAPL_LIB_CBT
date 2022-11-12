#include "rapl.h"


int main()
{
	rapl rapl_inst;
	CallBackTimer *c=rapl_inst.CBT;
	cout <<"start" <<endl; 
	c->start(20,[&rapl_inst](void){
		rapl_inst.measure_energy_thread();});
	sleep(3);
	c->stop();
	cout <<"end" <<endl; 

	return 0;
}

