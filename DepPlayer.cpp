#include "dante/Buffers.hpp"
#include "dante/Priority.hpp"
#include <assert.h>
#include <signal.h>
#include <string.h>
#ifdef WIN32
#else
#include <unistd.h>
#endif

#include <iostream>
#include <fstream>
#include <sys/ioctl.h>
#include <stdio.h>
#include <math.h>

#include <ThreadedDSP.hpp>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Configuration - size and structure details
//


#define	SHM_NAME	"DepConvolver"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data structures and Static Variables
//

char sScreen[256*256];
ThreadedDSP DSP;
Dante::Buffers Buffers;
Dante::Runner  Runner(Buffers);
float fIn [DSP.MaxChannels] = {};
float fOut[DSP.MaxChannels] = {};
bool bKill;
float Filt[DSP.MaxLength]={}; 

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event handlers
//
static bool g_running = true;
static void signal_handler(int sig)
{
	(void) sig;
	g_running = false;
	bKill = true;
	signal(SIGINT, signal_handler);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Display - Text Meters
//
#define paint(x,y,c)    { if ((x)<nWidth && (y)<nHeight) s[(nHeight-(y)-1)*(nWidth+1)+(x)]=c; }
#define row(x,y,xx,c)   { for (int __n=(x); __n<(x)+(xx); __n++) paint(__n,y,c); }
#define col(x,y,yy,c)   { for (int __n=(y); __n<(y)+(yy); __n++) paint(x,__n,c); }
void meters(char *s, int nWidth, int nHeight, int nIn, int nOut, float *pfIn, float *pfOut)
{
	memset(s,' ',nHeight*(nWidth+1)); for (int n=0;n<nHeight;n++) { s[n*(nWidth+1)+nWidth]='\n'; };
	s[nHeight*(nWidth+1)]=0;

	int   nBar   = nHeight-3;
	float fScale = 1.0F/5.0F; 

	col(3,3,nBar,'|');
	col(3+nIn+1,3,nBar,'|');
	col(3+nIn+3,3,nBar,'|');
	col(3+nIn+3+nOut+1,3,nBar,'|');
	row(3,2,nIn+2,'-');
	row(3+nIn+3,2,nOut+2,'-');

	for (int n=0; n<9; n++) 
	{ 
		int nH = nHeight-(int)(n*10*fScale+0.5)-1; 
		if (nH<3) break;
		if (n>0) paint(0,nH,'-'); paint(1,nH,'0'+n); paint(2,nH,'0');
		if (n>0) paint(8+nIn+nOut,nH,'-'); paint(9+nIn+nOut,nH,'0'+n); paint(10+nIn+nOut,nH,'0');
	}

	static char S[] = { ' ', '_', '.', 'x', 'X'};

	for (int n=0; n<nIn; n++) 
	{ 
		paint(4+n,0,(n+1)%10+'0');
		paint(4+n,1,(n+1)/10+'0');
		float h = nBar+fScale*20.0F*log10f(pfIn[n]+1E-12F);
		if (h>0)
		{
			col  (4+n,3,(int)h,'X');
			paint(4+n,3+(int)h,S[(int)((h-(int)h)*5)]);
		}
	};
	
	for (int n=0; n<nOut; n++) 
	{ 
		paint(4+nIn+3+n,0,(n+1)%10+'0'); 
		paint(4+nIn+3+n,1,(n+1)/10+'0');
		float h = nBar+fScale*20.0F*log10f(pfOut[n]+1E-12F);
		if (h>0)
		{
			col  (4+nIn+3+n,3,(int)h,'X');
			paint(4+nIn+3+n,3+(int)h,S[(int)((h-(int)h)*5)]);
		}
	};
}

static int nWait;

void dep(void)
{
	setDantePriority("DepConvolver");

	if (Buffers.connect("DanteEP", false)) 
	{ std::cerr << "Cannot connect to DEP." << std::endl; exit(0); };

	const Dante::buffer_header_t* header = Buffers.getHeader();
	int 	 nTx 	 = std::min(DSP.Outputs(),(int)header->audio.num_tx_channels);
	int 	 nRx 	 = std::min(DSP.Inputs(),(int)header->audio.num_rx_channels);
	int 	 nReset  = -1;
	uint64_t nPeriod = 0;
	int32_t  Temp[DSP.MaxBlock] = {};


	Dante::Timing timing;
	int result = timing.open(Buffers.getTimingObjectSubheader(), Buffers.isGlobalNamespace());
	if (result)
	{
		std::cerr << "Error opening timing object: " << Dante::Timing::getErrorMessage(result) << std::endl;
	}

	while (g_running && header->metadata.magic_marker && DSP.Running())
	{
		if (nReset != header->metadata.reset_count)
		{
			nReset  = header->metadata.reset_count;
			nPeriod = header->time.period_count;
			continue;
		}

		int nPeriodsPerBlock = DSP.BlockSize() / header->time.samples_per_period;
		int chunk;
		while (*(volatile uint32_t *)&header->time.period_count < nPeriod + nPeriodsPerBlock ) { nWait++; timing.wait(); };		// Sync to Dante cadence
		nPeriod += nPeriodsPerBlock;


		uint64_t     nPeriodCount       = header->time.period_count;
		int 		 nBlock             = DSP.BlockSize();
		int          nSamplesPerPeriod  = header->time.samples_per_period;
		int          nSamplesPerChannel = header->audio.samples_per_channel;
		int          nLatency           = DSP.Latency();
		unsigned int nRxHead = (unsigned int) (((nPeriod-nPeriodsPerBlock)*nSamplesPerPeriod           ) % nSamplesPerChannel);		// Use data one Block before the
		unsigned int nTxHead = (unsigned int) (((nPeriod-nPeriodsPerBlock)*nSamplesPerPeriod + nLatency ) % nSamplesPerChannel);		// actual Dante heads

		if (nPeriodCount > nPeriod)	DSP.Chrono_N(0)->count((nPeriodCount - nPeriod)*nSamplesPerPeriod); 		// Log the call time against sample clock
		if (nPeriodCount > nPeriod + 10*nPeriodsPerBlock) nPeriod = header->time.period_count; 				    // If we are way off, reset the pointer

		chunk = nSamplesPerChannel - nRxHead;
		if (chunk>=nBlock)  for (int n = 0; n < nRx; n++) DSP.Input (n, (int32_t *)Buffers.getDanteRxChannel(n)+nRxHead,1);
		else
		{
			for (int n = 0; n < nRx; n++)
			{
				memcpy(Temp,      (int32_t *)Buffers.getDanteRxChannel(n)+nRxHead,    chunk    *sizeof(int32_t));
				memcpy(Temp+chunk,(int32_t *)Buffers.getDanteRxChannel(n),        (nBlock-chunk)*sizeof(int32_t));
				DSP.Input(n, Temp, 1);
			} 
		}
		
		DSP.Process();
		DSP.Finish();

		chunk = nSamplesPerChannel - nTxHead;
		if (chunk>=nBlock)  for (int n = 0; n < nTx; n++) DSP.Output(n, (int32_t *)Buffers.getDanteTxChannel(n)+nTxHead,1);
		else
		{
			for (int n = 0; n < nTx; n++)
			{
				DSP.Output(n, Temp, 1);
				memcpy((int32_t *)Buffers.getDanteTxChannel(n)+nTxHead,Temp,         chunk     *sizeof(int32_t));
				memcpy((int32_t *)Buffers.getDanteTxChannel(n),        Temp+chunk,(nBlock-chunk)*sizeof(int32_t));
			} 
		}

		
/*		chunk = std::min(nSamplesPerChannel-(int)nRxHead,nBlock);
		memcpy(Temp,      (int32_t *)Buffers.getDanteRxChannel(0)+nRxHead,    chunk    *sizeof(int32_t));
		if (chunk<nBlock) memcpy(Temp+chunk,(int32_t *)Buffers.getDanteRxChannel(0),        (nBlock-chunk)*sizeof(int32_t));
		chunk = std::min(nSamplesPerChannel-(int)nTxHead,nBlock);
		memcpy((int32_t *)Buffers.getDanteTxChannel(0)+nTxHead,Temp,         chunk     *sizeof(int32_t));
		if (chunk<nBlock) memcpy((int32_t *)Buffers.getDanteTxChannel(0),        Temp+chunk,(nBlock-chunk)*sizeof(int32_t));
*/		

		int time = ((int)header->time.period_count - nPeriod)*nSamplesPerPeriod - nLatency + nBlock;
		DSP.Chrono_N(1)->count(time);  

	}
	timing.close();
	Buffers.disconnect();
}


int main(int argc, char * argv[])
{

	// PARSE COMMAND LINE OPTIONS
	bool bClear=false, bReset=false, bDSP=false, bSilent=false, bLogY=false;
	int  nDSP[]={ 16, 16, 64, 32, 144, 1, 1}, nTestLength=0;
	const char *sFile="";

	while (argc>1)
	{
		argv++; argc--;		// Skip executable
		int nVal;
		if (*argv[0]=='-') switch (tolower(argv[0][1]))
		{
			case 'c' : bClear=true;  break;
			case 'r' : bReset=true;  break;
			case 's' : bSilent=true; break;			
			case 'l' : bLogY=true;   break;			
			case 'd' : bDSP=true;    for (int n=0;n<7;n++) { argv++; argc--; if (!argc) break; sscanf(argv[0],"%d",nDSP+n); }; break; 
			case 'x' : argv++; argc--; if (!argc) break; sscanf(argv[0],"%d",&nVal); nTestLength=nVal; break;  	
			case 'k' : bKill=true; break;
			default: printf("Argument Error : Usage  \nDepConvolver [ -s(ilent) -c(lear) -(r)eset -l(ogY) -d(sp) rx tx block blocks latency filters threads -x testlength -(k)ill ] filter_file\n"); exit(0);
		}
		else { sFile=argv[0]; break; };
	}

	//bDSP = true;
	//nTestLength = 32;

	signal(SIGINT, signal_handler);
	struct winsize w;
	char *s = (char *)calloc(1024*256,sizeof(char));

	if ( ( bDSP && !DSP.Create(nDSP[2],nDSP[3],nDSP[0],nDSP[1],nDSP[5],0,SHM_NAME,nDSP[6],nDSP[4])) ||
	     (!bDSP && !DSP.Attach(SHM_NAME) ) )	
	{	std::cerr << "Unable to " << (bDSP?"create":"attach") << " shared memory for Convolver at " << SHM_NAME << std::endl; exit(0); };

	if (bKill) { DSP.Stop(); exit(0); }; 

	if (DSP.Owner())
	{
		DSP.Chrono_N(0)->config(0,800,101,COUNTER, "COUNT OF LATE CALL EVENTS (samples)");
		DSP.Chrono_N(1)->config(-400,400,101,COUNTER,"COUNT OF DSP OUTPUT TIMES (samples)");
	    std::thread *pthread = new std::thread(dep);
	}

	Filt[0]=1.0F;
	if (bReset) for (int n=0; n<DSP.Inputs(); n++) for (int m=0; m<DSP.Outputs(); m++) DSP.LoadFilter(n,m); 
	if (nTestLength) for (int n=0; n<DSP.MaxFilters; n++) DSP.LoadFilter(((n*(std::max(nDSP[0],nDSP[1])-1))/nDSP[1])%nDSP[0],(n*(std::max(nDSP[0],nDSP[1])-1))%nDSP[1],std::min((int)(sizeof(Filt)/sizeof(float)),nTestLength),Filt);

	if (bClear || bReset) DSP.Chrono_Reset();
	
	Histogram hTemp;

	std::ifstream file;  
	if (sFile[0])
	{
		std::ifstream file(sFile);  
		if (!file.is_open()) { std::cerr << "WARNING Could not load filters" << std::endl; };
		while (file.is_open() && !file.eof())
		{
			std::string str;
			std::getline(file,str);
			if (str.find("FILTER")!=std::string::npos)
			{
				int nIn=0, nOut=0, nLength=0;
				sscanf(str.c_str(),"FILTER %*s Length= %d In= %d Out= %d",&nLength,&nIn,&nOut);
				if (nLength && nIn && nOut)
				{
					for (int n=0; n<nLength; n++) file >> Filt[n];
					DSP.LoadFilter(nIn-1,nOut-1,nLength,Filt);
					printf("FILTER Loaded IN=%-2d  OUT=%-2d  Length=%6d\n",nIn,nOut,nLength);
				} 
			}
		}
	}
	

	while(g_running && DSP.Running())
	{
		usleep(200000);
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		if (!bDSP && bSilent) { bKill=true; break; }; 
		if (w.ws_row<20 || bSilent) continue;
		for (int n=0; n<DSP.Inputs(); n++) fIn[n] = DSP.PeakIn(n); 
		for (int n=0; n<DSP.Outputs(); n++) fOut[n] = DSP.PeakOut(n);

		meters(s, w.ws_col, w.ws_row-2 - 4*(w.ws_row/5), DSP.Inputs(), DSP.Outputs(), fIn, fOut);
		int ret = system("clear");
		printf("%s [%s] Block %12ld   Taps %8d\n",DSP.Owner()?"Running":"Attached",DSP.SHM_Name(),DSP.Count(),DSP.Taps());
		printf("%s",s);

		DSP.Chrono_CallTime()->histogram(&hTemp);
		HistTextOption options = X_LABEL | Y_LABEL |  TOTAL | MEAN | MODE | MAX;
		if (bLogY) options = options | LOGY;
		hTemp.text(w.ws_row/5-2, s, options);
		printf("%s",s);
		DSP.Chrono_Load()->histogram(&hTemp);
		hTemp.text(w.ws_row/5-2, s, options);
		printf("%s",s);
		DSP.Chrono_N(0)->histogram(&hTemp);
		hTemp.text(w.ws_row/5-2, s, options);
		printf("%s",s);
		DSP.Chrono_N(1)->histogram(&hTemp);
		hTemp.text(w.ws_row/5-2, s, options);
		printf("%s",s);

	}
	usleep(50000);
	cleanupDantePriority();
	if (!DSP.Owner() && DSP.Running()) printf("DSP STATE %12ld Rx=%d Tx=%d N=%d M=%d L=%d F=%d T=%d   Filters=%d UsedTaps=%d\n",DSP.Count(), DSP.Inputs(),DSP.Outputs(),DSP.BlockSize(),DSP.Blocks(),DSP.Latency(),DSP.Filters(),DSP.Threads(),DSP.Filters(),DSP.Taps());
	if (!bKill)	std::cerr << "Unexpected Fail : Possibly Insufficient CPU for sustaining DSP" << std::endl;
	return 0;
}


#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

    const char *       ATS_CORE_GIT_HASH_FULL = "";
    const char *       ATS_CORE_GIT_HASH_SHORT = "";
    const bool         ATS_CORE_GIT_STATE_CHANGES = false;
    const char *       ATS_CORE_GIT_COMPONENT_TAG_HASH = "";
    const char *       ATS_CORE_GIT_COMPONENT_TAG_HASH_SHORT = "";
    const char *       ATS_CORE_GIT_COMPONENT_TAG = "";
    const char *       ATS_CORE_GIT_COMPONENT_TAG_VERSION = "";
    const bool         ATS_CORE_GIT_COMMITS_SINCE_COMPONENT_TAG = false;
    const char *       ATS_CORE_GIT_COMPONENT_TAG_VERSION_TYPE = "";
    const char *       ATS_CORE_GIT_COMPONENT_TAG_VERSION_FULL = "";
    const char *	   ATS_CORE_GIT_VERSION_MAJOR = "";
    const char *       ATS_CORE_GIT_VERSION_MINOR = "";
    const char *       ATS_CORE_GIT_VERSION_PATCH = "";
    const char *       ATS_CORE_GIT_VERSION_SUFFIX = "";

#ifdef __cplusplus
}
#endif