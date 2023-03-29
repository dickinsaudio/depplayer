#include "dante/Buffers.hpp"
#include "dante/Priority.hpp"
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <thread>

#include <iostream>
#include <fstream>
#include <sys/ioctl.h>
#include <stdio.h>
#include <math.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Data structures and Static Variables
//

Dante::Buffers Buffers;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event handlers
//
static bool g_running = true;
static void signal_handler(int sig)
{
	(void) sig;
	g_running = false;
	signal(SIGINT, signal_handler);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// HELPER FUNCTIONS
// wav reader

struct wav_t
{
	int			length;
	int			channels;
	int			rate;
	int16_t*	data;
};

wav_t* wav_load (char* file)
{
	wav_t *p;
	FILE *f;
	f = fopen(file,"rb");
	if (f==nullptr) return nullptr;
	uint16_t format;    fseek(f, 20, SEEK_SET); fread(&format, 2, 1, f);			if (format!=1) return 0;
	uint16_t channels;  fseek(f, 22, SEEK_SET); fread(&channels, 2, 1, f);		if (channels<1 || channels>64) return 0;
	uint16_t rate;      fseek(f, 24, SEEK_SET); fread(&rate, 2, 1, f);			
	char chunk[5]; chunk[4]='\0';
	int32_t size;
	fseek(f,12,SEEK_SET);
	fread(&chunk,4,1,f);
	while(!feof(f) && strcmp(chunk,"data")!=0)
	{
		fread(&size,4,1,f);
		fseek(f,size,SEEK_CUR);
		fread(&chunk,4,1,f);
	}
	if (strcmp(chunk,"data")!=0) return nullptr;
	fread(&size,4,1,f);

	p = (wav_t*)calloc(1,sizeof(wav_t));
	p->channels = channels;
	p->length   = size/channels/2;
	p->rate     = rate;

	p->data = (int16_t *)calloc(1,size);
	fread(p->data,size,1,f);

	return p;
}

int				wav_length  (wav_t* p)	{ if (p!=nullptr) return p->length; return 1;}
int				wav_channels(wav_t* p)	{ if (p!=nullptr) return p->channels;  return 0; }
int16_t			wav_get     (wav_t* p, int sample, int channel) { if (p!=nullptr && sample<p->length && channel<p->channels) return p->data[p->channels*sample+channel]; return 0; }
void            wav_destroy (wav_t* p)	{ if (p!=nullptr) { if (p->data != nullptr) free(p->data); free(p); } }


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN DEP CODE

char sEmpty[1] = "";
char *sFile=sEmpty;
int nLoops = -1;
int ndB  = -20;

void dep(void)
{
	setDantePriority("DepConvolver");

	if (Buffers.connect("DanteEP", false)) 
	{ std::cerr << "Cannot connect to DEP." << std::endl; exit(0); };

	wav_t *wav = wav_load(sFile);
	if (wav==nullptr) { g_running = false; return; };

	const Dante::buffer_header_t* header = Buffers.getHeader();
	int 	 nTx 	 = std::min(wav_channels(wav),(int)header->audio.num_tx_channels);
	uint64_t nPeriod = 0;
	int      nSample = 0;

	Dante::Timing timing;
	int result = timing.open(Buffers.getTimingObjectSubheader(), Buffers.isGlobalNamespace());
	if (result)
	{
		std::cerr << "Error opening timing object: " << Dante::Timing::getErrorMessage(result) << std::endl;
	}

	int PERIODS = 4;		// Number of periods to play at once

	while (g_running && header->metadata.magic_marker && nLoops != 0)
	{
		nPeriod += PERIODS;
		while (*(volatile uint32_t *)&header->time.period_count < nPeriod ) 
		{
			 if (header->time.period_count > nPeriod + 10 || header->time.period_count + 10 < nPeriod) { nPeriod = header->time.period_count; continue; } 
			 timing.wait(); 
		};	

		uint64_t     nPeriodCount       = header->time.period_count;
		int          nSamplesPerPeriod  = header->time.samples_per_period;
		int          nSamplesPerChannel = header->audio.samples_per_channel;
		unsigned int nTxHead = (unsigned int) ((nPeriod*nSamplesPerPeriod + 48 ) % nSamplesPerChannel);		// actual Dante heads

		for (int n=0; n<PERIODS * nSamplesPerPeriod; n++)
		{
			for (int c=0; c<nTx; c++)	*((int32_t *)Buffers.getDanteTxChannel(c)+nTxHead) = wav_get(wav,nSample,c);
			nTxHead = (nTxHead+1)%nSamplesPerChannel;
			nSample++;
			if (nSample >= wav_length(wav)) { nSample=0; nLoops--; if (nLoops==0) break; };
		}
	}
	timing.close();
	Buffers.disconnect();
}


int main(int argc, char * argv[])
{

	// PARSE COMMAND LINE OPTIONS
	while (argc>1)
	{
		argv++; argc--;		// Skip executable
		if (*argv[0]=='-') switch (tolower(argv[0][1]))
		{
			case 'n' : argv++; argc--; if (!argc) break; sscanf(argv[0],"%d",&nLoops); break;  	
			case 'g' : argv++; argc--; if (!argc) break; sscanf(argv[0],"%d",&ndB); break;  	
			default: printf("Argument Error : Usage  \ndepplayer  [ -n loops | -g gain_dB  ] file.wav\n"
			                "    loops   Number of loops to play -1 or 1 .. N  default -1 (continuous)\n"
							"    gain_db Gain to apply in dB     -inf to +20   default -20\n"); exit(0);
		}
		else { sFile=argv[0]; break; };
		if (ndB>20) ndB = 20;

	}

	signal(SIGINT, signal_handler);
	std::thread *pthread = new std::thread(dep);


	while(g_running)
	{
		usleep(200000);
	}
	usleep(50000);
	cleanupDantePriority();
	return 0;
}
