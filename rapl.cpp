/* Read the RAPL registers on recent (>sandybridge) Intel processors	*/
/*									*/
/* There are currently three ways to do this:				*/
/*	1. Read the MSRs directly with /dev/cpu/??/msr			*/
/*	2. Use the perf_event_open() interface				*/
/*	3. Read the values from the sysfs powercap interface		*/
/*									*/
/* MSR Code originally based on a (never made it upstream) linux-kernel	*/
/*	RAPL driver by Zhang Rui <rui.zhang@intel.com>			*/
/*	https://lkml.org/lkml/2011/5/26/93				*/
/* Additional contributions by:						*/
/*	Romain Dolbeau -- romain @ dolbeau.org				*/
/*									*/
/* For raw MSR access the /dev/cpu/??/msr driver must be enabled and	*/
/*	permissions set to allow read access.				*/
/*	You might need to "modprobe msr" before it will work.		*/
/*									*/
/* perf_event_open() support requires at least Linux 3.14 and to have	*/
/*	/proc/sys/kernel/perf_event_paranoid < 1			*/
/*									*/
/* the sysfs powercap interface got into the kernel in 			*/
/*	2d281d8196e38dd (3.13)						*/
/*									*/
/* Vince Weaver -- vincent.weaver @ maine.edu -- 11 September 2015	*/
/*									*/

#include "rapl.h"
void CallBackTimer::stop()
{
	_execute.store(false, std::memory_order_release);
	if( _thd.joinable() )
		_thd.join();
	//cout << "Thread stop!"<<endl;
}

//void start(int interval, std::function<void(void)> func)
void CallBackTimer::start(int interval, std::function<void(void)> func)
{
	if( _execute.load(std::memory_order_acquire) ) {
		stop();
	};
	//cout << "Thread start!"<<endl;

	_execute.store(true, std::memory_order_release);

	_thd = std::thread([this, interval, func]()
			{
			while (_execute.load(std::memory_order_acquire)) {
			func();                   
			std::this_thread::sleep_for(
					std::chrono::milliseconds(interval));
			}
			});
}

bool CallBackTimer::is_running() const noexcept {
	return ( _execute.load(std::memory_order_acquire) && 
			_thd.joinable() );
}


int rapl::detect_cpu(void) {

	FILE *fff;

	int vendor=-1,family,model=-1;
	char buffer[BUFSIZ],*result;
	char vendor_string[BUFSIZ];

	fff=fopen("/proc/cpuinfo","r");
	if (fff==NULL) return -1;

	while(1) {
		result=fgets(buffer,BUFSIZ,fff);
		if (result==NULL) break;

		if (!strncmp(result,"vendor_id",8)) {
			sscanf(result,"%*s%*s%s",vendor_string);

			if (!strncmp(vendor_string,"GenuineIntel",12)) {
				vendor=CPU_VENDOR_INTEL;
			}
			if (!strncmp(vendor_string,"AuthenticAMD",12)) {
				vendor=CPU_VENDOR_AMD;
			}
		}

		if (!strncmp(result,"cpu family",10)) {
			sscanf(result,"%*s%*s%*s%d",&family);
		}

		if (!strncmp(result,"model",5)) {
			sscanf(result,"%*s%*s%d",&model);
		}

	}

	if (vendor==CPU_VENDOR_INTEL) {
		if (family!=6) {
			printf("Wrong CPU family %d\n",family);
			return -1;
		}

		msr_rapl_units=MSR_INTEL_RAPL_POWER_UNIT;
		msr_pkg_energy_status=MSR_INTEL_PKG_ENERGY_STATUS;
		msr_pp0_energy_status=MSR_INTEL_PP0_ENERGY_STATUS;

		printf("Found ");

		switch(model) {
			case CPU_SANDYBRIDGE:
				printf("Sandybridge");
				break;
			case CPU_SANDYBRIDGE_EP:
				printf("Sandybridge-EP");
				break;
			case CPU_IVYBRIDGE:
				printf("Ivybridge");
				break;
			case CPU_IVYBRIDGE_EP:
				printf("Ivybridge-EP");
				break;
			case CPU_HASWELL:
			case CPU_HASWELL_ULT:
			case CPU_HASWELL_GT3E:
				printf("Haswell");
				break;
			case CPU_HASWELL_EP:
				printf("Haswell-EP");
				break;
			case CPU_BROADWELL:
			case CPU_BROADWELL_GT3E:
				printf("Broadwell");
				break;
			case CPU_BROADWELL_EP:
				printf("Broadwell-EP");
				break;
			case CPU_SKYLAKE:
			case CPU_SKYLAKE_HS:
				printf("Skylake");
				break;
			case CPU_SKYLAKE_X:
				printf("Skylake-X");
				break;
			case CPU_KABYLAKE:
			case CPU_KABYLAKE_MOBILE:
				printf("Kaby Lake");
				break;
			case CPU_KNIGHTS_LANDING:
				printf("Knight's Landing");
				break;
			case CPU_KNIGHTS_MILL:
				printf("Knight's Mill");
				break;
			case CPU_ATOM_GOLDMONT:
			case CPU_ATOM_GEMINI_LAKE:
			case CPU_ATOM_DENVERTON:
				printf("Atom");
				break;
			default:
				printf("Unsupported model %d\n",model);
				model=-1;
				break;
		}
	}

	if (vendor==CPU_VENDOR_AMD) {

		msr_rapl_units=MSR_AMD_RAPL_POWER_UNIT;
		msr_pkg_energy_status=MSR_AMD_PKG_ENERGY_STATUS;
		msr_pp0_energy_status=MSR_AMD_PP0_ENERGY_STATUS;

		if (family!=23) {
			printf("Wrong CPU family %d\n",family);
			return -1;
		}
		model=CPU_AMD_FAM17H;
	}

	fclose(fff);

	printf(" Processor type\n");

	return model;
}
int rapl::detect_packages(void) {
	char filename[BUFSIZ];
	FILE *fff;
	int package;
	int i;
	for(i=0;i<MAX_PACKAGES;i++) package_map[i]=-1;

	printf("\t");
	for(i=0;i<MAX_CPUS;i++) {
		sprintf(filename,"/sys/devices/system/cpu/cpu%d/topology/physical_package_id",i);
		fff=fopen(filename,"r");
		if (fff==NULL) break;
		fscanf(fff,"%d",&package);
		printf("%d (%d)",i,package);
		if (i%8==7) printf("\n\t"); else printf(", ");
		fclose(fff);

		if (package_map[package]==-1) {
			total_packages++;
			package_map[package]=i;
		}

	}
	printf("\n");
	total_cores=i;
	printf("\tDetected %d cores in %d packages\n\n",
			total_cores,total_packages);
	return 0;
}
void rapl::measure_init()
{
	int i,j;
	FILE *fff;
	char tempfile[256];
	printf("\nTrying sysfs powercap interface to gather results\n\n");
	//cout << "total_packages="<<total_packages <<endl;

	/* /sys/class/powercap/intel-rapl/intel-rapl:0/ */
	/* name has name */
	/* energy_uj has energy */
	/* subdirectories intel-rapl:0:0 intel-rapl:0:1 intel-rapl:0:2 */

	for(j=0;j<total_packages;j++) {
		i=0;
		sprintf(basename[j],"/sys/class/powercap/intel-rapl/intel-rapl:%d",
			j);
		sprintf(tempfile,"%s/name",basename[j]);
		fff=fopen(tempfile,"r");
		if (fff==NULL) {
			fprintf(stderr,"\tCould not open %s\n",tempfile);
			return;
		}
		fscanf(fff,"%s",event_names[j][i]);
		valid[j][i]=1;
		fclose(fff);
		sprintf(filenames[j][i],"%s/energy_uj",basename[j]);

		/* Handle subdomains */
		for(i=1;i<NUM_RAPL_DOMAINS;i++) {
			sprintf(tempfile,"%s/intel-rapl:%d:%d/name",
				basename[j],j,i-1);
			fff=fopen(tempfile,"r");
			if (fff==NULL) {
				//fprintf(stderr,"\tCould not open %s\n",tempfile);
				valid[j][i]=0;
				continue;
			}
			valid[j][i]=1;
			fscanf(fff,"%s",event_names[j][i]);
			fclose(fff);
			sprintf(filenames[j][i],"%s/intel-rapl:%d:%d/energy_uj",
				basename[j],j,i-1);

		}
	}

	/* Gather initial values */
	for(j=0;j<total_packages;j++) {
		for(i=0;i<NUM_RAPL_DOMAINS;i++) {
			if (valid[j][i]) {
				sum[j][i]=0;
				fff=fopen(filenames[j][i],"r");
				if (fff==NULL) {
					fprintf(stderr,"\tError opening %s!\n",filenames[j][i]);
				}
				else {
					fscanf(fff,"%lli",&before[j][i]);
					fclose(fff);
				}
			}
		}
	}
}

void rapl::measure_start()
{
	//rapl.CBT->start(rapl.interval,[&rapl](void){rapl.measure_energy_thread();});
	static int restart = 0;
	char temp[256];
	int i,j;
	sprintf(temp,"output_%s_%dms",name,interval);
	if (restart == 0)
	{
		ofile=fopen(temp,"w");
		for(j=0;j<total_packages;j++) {
			for(i=0;i<NUM_RAPL_DOMAINS;i++) {
				if (valid[j][i]) {
					fprintf(ofile,"%s,",event_names[j][i]);
				}
			}
			fseek (ofile , -1 , SEEK_CUR);
			fprintf(ofile,"\n");
		}
		restart = 1;
	}
	else 
	{
		cout << "Measure restart" << endl;
		ofile=fopen(temp,"a");
	}
	cout << "Measure start" << endl;
	CBT->start(interval,[this](void){this->measure_energy_thread();});
}
void rapl::measure_stop()
{
	CBT->stop();
	fclose(ofile);
	cout << "Measure stop" << endl;
}

void rapl::measure_energy_thread()
{
	int i,j;
	long long diff=0;
	FILE *fff;
	/* Gather current values */
	for(j=0;j<total_packages;j++) {
		for(i=0;i<NUM_RAPL_DOMAINS;i++) {
			if (valid[j][i]) {
				fff=fopen(filenames[j][i],"r");
				if (fff==NULL) {
					fprintf(stderr,"\tError opening %s!\n",filenames[j][i]);
				}
				else {
					fscanf(fff,"%lld",&after[j][i]);
					fclose(fff);
				}
			}
		}
	}

	for(j=0;j<total_packages;j++) {
		//printf("\tPackage %d\n",j);
		for(i=0;i<NUM_RAPL_DOMAINS;i++) {
			if (valid[j][i]) {
				//diff = ((double)after[j][i]-(double)before[j][i])/1000000.0;
				diff = after[j][i]-before[j][i];
				sum[j][i] += diff;
				//fprintf(ofile,"%10lli,",diff);
				fprintf(ofile,"%lli,",diff);
			}
		}
		fseek (ofile , -1 , SEEK_CUR);
		fprintf(ofile,"\n");
	}
	memcpy(before, after, sizeof before);
}

