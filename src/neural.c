#include <neural.h>
#include <video.h>
#include <audio.h>
#include <shapes.h>
#include <text.h>
#include <bandpass.h>

#include <math.h>
#include <stdio.h>

#define DATASIZE 100
#define MAX_SPIKES 100
#define NCOLORS 16
#define SPIKE_WIDTH 60
#define TRACESIZE 150000
#define NPROBES 2

int TARGET_CHANNEL,liveplay;

struct raster_point {
	int t;
	struct raster_point* tail;
};

struct Spike
{
	float data[SPIKE_WIDTH];
	float timestamp;
	int active;
};

typedef struct SpikeLine
{
	struct bandpass_struct	bp;
	struct Spike 			spikes[MAX_SPIKES];
	unsigned int			currSpike;
	int						enabled,spikecount,tracesize;
	float 					data[DATASIZE],*tracedata[2];
}SpikeLine;

static const char *vertexShaderSource = "#version 130\n"
	"#extension GL_ARB_explicit_attrib_location : enable\n"
    "layout (location = 0) in float value;\n"
	"uniform float t;"
	"uniform float yscale;"
	"uniform float ylimit;"
	"uniform vec4 pos_scale;"
	"uniform vec4 tscale;"
	"uniform vec4 aColor;"
	"out vec4 ourColor;\n"
    "void main()\n"
    "{\n"
	"	vec4 p = vec4(((gl_VertexID/t*2.f-1.f)), (value*yscale), 0.0, 1.0);"
	"	float y = p.y;\n"
	"	if(y > 1.f*ylimit)\n"
	"		y = 1.f*ylimit;\n"
	"	else if(y < -1.f*ylimit)\n"
	"		y = -1.f*ylimit;\n"
	"	p = vec4(p.x*pos_scale.z+pos_scale.x,y*pos_scale.w+pos_scale.y,0.0,1.0);"
    "   gl_Position = vec4(p.x*tscale.z+tscale.x,p.y*tscale.w+tscale.y,0.0,1.0);\n"
	"	ourColor = aColor;"
    "}\0";

static const char *fragmentShaderSource = "#version 130\n"
	"#extension GL_ARB_explicit_attrib_location : enable\n"
    "out vec4 FragColor;\n"
	"in vec4 ourColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = ourColor;\n"
    "}\n\0";

static float colors[][3] = {
    {1.0, 0.0, 0.0},
    {1.0, 0.498, 0.0},
    {1.0, 1.0, 0.0},
    {0.498, 1.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 1.0, 0.498},
    {0.0, 1.0, 1.0},
    {0.0, 0.498, 1.0},
    {0.0, 0.0, 1.0},
    {0.498, 0.0, 1.0},
    {1.0, 0.0, 1.0},
    {1.0, 0.0, 0.498},
    {1.0, 0.333, 0.333},
    {1.0, 0.667, 0.0},
    {1.0, 1.0, 0.333},
    {0.667, 1.0, 0.0}
};

//GL Variables
static unsigned int lineVAO, lineVBO;
static int shader,col_loc,pos_loc,ts_loc,ys_loc,ylim_loc,t_loc;

//
static int NCHANNELS, AUDIOTYPE = 1, SAMPLE_RATE, DATA_STRIDE, STRIDE_BYTES, DCOUNT = 0, DTRACE = 0, TRACEBUF = 0;
static float *filebuffer, audiobuffer[DATASIZE];

HANDLE hFile,rFile;

static SpikeLine *spikeline, *rasterline;
static struct raster_point **raster_head,**raster_tail;

static void add_raster(int i, int time) {
	if(raster_head[i] == NULL) {
		raster_head[i] = (struct raster_point*)malloc(sizeof(struct raster_point));
		raster_head[i]->t = time;
		raster_head[i]->tail = NULL;
		raster_tail[i] = raster_head[i];
	}
	else if(time > raster_tail[i]->t) {
		struct raster_point* rp = (struct raster_point*)malloc(sizeof(struct raster_point));
		rp->t = time;
		rp->tail = NULL;
		
		raster_tail[i]->tail = rp;
		raster_tail[i] = rp;
	}
}

static void pop_raster(int i) {
	if(raster_head[i] != NULL) {
		struct raster_point* rp = raster_head[i];
		raster_head[i] = raster_head[i]->tail;
		if(raster_tail[i]->t == rp->t)
			raster_tail[i] = NULL;
		
		free(rp);
	}
}

static int openfile(HANDLE* fileHandle, char* filename)
{
	*fileHandle = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL,
                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if(*fileHandle == INVALID_HANDLE_VALUE) {
	  fprintf(stderr,"CAN'T OPEN DATA FILE: %ld",GetLastError());
	  return 0;
	}
	
	return 1;
}

static int seekfile(HANDLE* fileHandle, double time)
{
	LARGE_INTEGER FILE_SIZE;
	LARGE_INTEGER li;
	int64_t t;
	
	if(!GetFileSizeEx(*fileHandle, &FILE_SIZE)) {
	  fprintf(stderr,"CAN'T GET FILE SIZE: %ld",GetLastError());
	  CloseHandle(*fileHandle);
	  return 0;
	}
	
	t = (int64_t)round(time)*(SAMPLE_RATE/1000)*DATA_STRIDE*sizeof(float);
	if(t < 0)
		t = 0;
	
	while(t % STRIDE_BYTES != 0)
		t--;
	
	li.QuadPart = t;

	if(!SetFilePointerEx(*fileHandle,li,NULL,FILE_BEGIN))
	{
		fprintf(stderr,"CAN'T SET FILE POSITION: %ld",GetLastError());
		CloseHandle(*fileHandle);
		return 0;
	}
	
	return 1;
}

int init_neural(char* filename, int nch, int nstr, int tsz, int srate, double tstart) {
	SAMPLE_RATE = srate;
	NCHANNELS = nch;
	DATA_STRIDE = nstr;
	STRIDE_BYTES = nstr*4;
	
	raster_head = malloc(sizeof(struct raster_point*)*NCHANNELS*2);
	raster_tail = malloc(sizeof(struct raster_point*)*NCHANNELS*2);
	if(!raster_head || !raster_tail)
		return 0;
	
	//Prepare file pointers
	if(!openfile(&hFile,filename) || !openfile(&rFile,filename))
		return 0;
	if(!seekfile(&hFile,tstart) || !seekfile(&rFile,tstart+1250))
		return 0;

	shader = create_program(vertexShaderSource,fragmentShaderSource);
	if(!shader)
		return 0;
	
	spikeline = malloc(sizeof(SpikeLine)*NCHANNELS*2);
	rasterline = malloc(sizeof(SpikeLine)*NCHANNELS*2);
	
	if(!spikeline || !rasterline)
		return 0;
	
	//INIT LINES
	memset(spikeline,0,sizeof(SpikeLine)*NCHANNELS*2);
	memset(rasterline,0,sizeof(SpikeLine)*NCHANNELS*2);
	
	for(int i = 0; i < NCHANNELS*2; i++) {
		init_bandpass(SAMPLE_RATE,3000.0,300.0,&spikeline[i].bp);
		init_bandpass(SAMPLE_RATE,3000.0,300.0,&rasterline[i].bp);
		spikeline[i].enabled = 1;
		
		spikeline[i].tracesize = tsz;
		for(int j = 0; j < 2; j++) {
			spikeline[i].tracedata[j] = malloc(sizeof(float)*tsz*2);
			
			if(!spikeline[i].tracedata[j])
				return 0;
			
			memset(spikeline[i].tracedata[j],0,sizeof(float)*tsz*2);
		}
		
		raster_head[i] = NULL;
		raster_tail[i] = NULL;
	}
	
	filebuffer = malloc(sizeof(float)*DATA_STRIDE);
	memset(filebuffer,0,sizeof(float)*DATA_STRIDE);
	
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);
		
	glBindVertexArray(lineVAO);
	
	glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
	
	glVertexAttribPointer(0, 1, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(0);
	
    glBindBuffer(GL_ARRAY_BUFFER, 0); 
	glBufferData(GL_ARRAY_BUFFER, sizeof(float)*spikeline->tracesize, spikeline[0].tracedata[0], GL_DYNAMIC_DRAW);

    glBindVertexArray(0); 
	
	glUseProgram(shader);
	
	col_loc = glGetUniformLocation(shader, "aColor");
	pos_loc = glGetUniformLocation(shader, "pos_scale");
	ts_loc = glGetUniformLocation(shader, "tscale");
	ys_loc = glGetUniformLocation(shader, "yscale");
	ylim_loc = glGetUniformLocation(shader, "ylimit");
	t_loc = glGetUniformLocation(shader, "t");
	
	return 1;
}

void draw_spikes(double time) {
	float x1 = -0.2, x2=x1+1.2;
	
	glUseProgram(shader);
	
	glBindVertexArray(lineVAO);
	glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
	
	glUniform1f(t_loc,SPIKE_WIDTH-1);
	glUniform1f(ys_loc,0.01);
	glUniform1f(ylim_loc,1.f);

	glUniform4f(ts_loc,0.45,0.f,0.5,1.f);
	for(int i = 0; i < NCHANNELS*2; i++)
	{
		int row = i%13;
		int column = i/13;
		if(i >= NCHANNELS)
		{
			row = (i+1)%13;
			column = (i+1)/13;
		}
		
		if(i < NCHANNELS)
			glUniform4f(pos_loc,column/10.f*2.f-1.f+1/10.f+x1,-(row/13.f*2.f-1.f)-1/13.f,1/10.f,1/13.f);
		else
			glUniform4f(pos_loc,(column-5)/10.f*2.f-1.f+1/10.f+x2,-(row/13.f*2.f-1.f)-1/13.f,1/10.f,1/13.f);
		for(int j = 0; j < MAX_SPIKES; j++)
		{
			if((time - spikeline[i].spikes[j].timestamp)/1000 > 2.f)
				continue;
			if(i == TARGET_CHANNEL)
				glUniform4f(col_loc,1.f,1.f,1.f,1.f-(time-spikeline[i].spikes[j].timestamp)/1000/2.f);
			else
				glUniform4f(col_loc,colors[i%NCOLORS][0],colors[i%NCOLORS][1],colors[i%NCOLORS][2],1.f-(time-spikeline[i].spikes[j].timestamp)/1000/2.f);
			
			glBufferData(GL_ARRAY_BUFFER, sizeof(float)*SPIKE_WIDTH, spikeline[i].spikes[j].data, GL_DYNAMIC_DRAW);
			glDrawArrays(GL_LINE_STRIP,0,SPIKE_WIDTH);
		}
	}
	
	//Draw Lines
	glUseProgram(shapes_->shader);
	glUniform4f(shapes_->colu_loc,0.45,0.45,0.45,1.f);
	glUniform4f(shapes_->ts_loc,0.45,0.f,0.5,1.f);
	for(int i = 0; i <= 13; i++)
	{
		if(i == 0)
		{
			draw_line(x1-.5,-0.999,0.5,0);
			draw_line(x2-.5,-0.999,0.5,0);
		}
		else if(i == 13)
		{
			draw_line(x1-.5,0.999,0.5,0);
			draw_line(x2-.5,0.999,0.5,0);
		}
		else
		{
			draw_line(x1-.5,i/13.f*2.f-1.f,0.5,0);
			draw_line(x2-.5,i/13.f*2.f-1.f,0.5,0);
		}
	}
	
	for(int i = 0; i <= 10; i++)
	{
		if(i <= 5)
			draw_line(x1+i/10.f*2.f-1.f,0.f,1.f,1);
		if(i >= 5)
			draw_line(x2-1+i/10.f*2.f-1.f,0.f,1.f,1);
	}
}

void draw_raster(double time, float lx, float ly) {
	float ar = ((float)video_->w/video_->h)/(4.f/3);
	int nrast = NCHANNELS*NPROBES;
	float rasterdur = 5000.f;
	
	//Draw Raster
	glUniform4f(shapes_->ts_loc,lx,-0.5+ly,0.45/ar,0.45);
	glUniform4f(shapes_->colu_loc,1.f,1.f,1.f,1.f);
	draw_line(0.f,-1.f,1.f,0);
	
	for(int i = 0; i < 5; i++)
		draw_line(i/4.f*2.f-1.f,-1.f,1/64.f,1);
	
	if(!liveplay)
		glLineWidth(2.f);
	for(int i = 0; i < nrast; i++) {
		struct raster_point* rp = raster_head[i];
		if(i == TARGET_CHANNEL)
			glUniform4f(shapes_->colu_loc,1.f,1.f,1.f,1.f);
		else
			glUniform4f(shapes_->colu_loc,colors[i%NCOLORS][0],colors[i%NCOLORS][1],colors[i%NCOLORS][2],1.f);
		while(rp != NULL) {
			if(time - rp->t > rasterdur) {
				pop_raster(i);
				rp = raster_head[i];
				continue;
			}
			
			draw_line(-((time-rp->t)/rasterdur*2.f-1.f),-((float)i/nrast*2.f-1.f),1.f/nrast,1);
			rp = rp->tail;
		} 
	}
	
	for (int i = 0; i < NPROBES; i++) {
		float y = 1.f-(i*(2.f/NPROBES)+1.f/NPROBES);
		glUniform4f(shapes_->colu_loc,i==0,i==2,i==1,1.f);
		draw_line(-1.f,y,1.f/NPROBES,1);
	}
	
	if(!liveplay)
		glLineWidth(1.f);
	
	glUniform4f(shapes_->colu_loc,1.f,1.f,1.f,0.85);
	draw_line(0.5,0.f,1.f,1);
	
	glUseProgram(text_->shader);
	glUniform4f(text_->colu_loc,1.f,1.f,1.f,1.f);
	glUniform4f(text_->ts_loc,lx,-0.5+ly,0.45,0.45);
	render_simpletext("\n0",0.5/ar,-1.f,5,TXT_CENTERED|TXT_BOTALIGNED);
	
	for (int i = 0; i < NPROBES; i++) {
		char buf[4];
		
		sprintf(buf,"%c ",'A'+i);
		render_simpletext(buf,-1.f/ar,1.f-(i*(2.f/NPROBES)+1.f/NPROBES),5,TXT_RGHTALIGNED);
	}
	
	//Draw bottom trace
	glUseProgram(shader);
	glBindVertexArray(lineVAO);
	glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
	
	glUniform1f(t_loc,TRACESIZE-1);
	glUniform1f(ys_loc,0.005);
	glUniform1f(ylim_loc,0.5);
	glUniform4f(col_loc,1.f,1.f,1.f,1.f);
	glUniform4f(ts_loc,lx,-1+2/16.f,0.45/ar,1/6.f);
	glUniform4f(pos_loc,0.f,0.f,1.f,1.f);
	
	glBufferData(GL_ARRAY_BUFFER, sizeof(float)*TRACESIZE, spikeline[TARGET_CHANNEL].tracedata[TRACEBUF]+DTRACE, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_LINE_STRIP,0,TRACESIZE);

	//Draw bottom border	
	glUseProgram(shapes_->shader);
	glUniform4f(shapes_->colu_loc,0.45,0.45,0.45,1.f);
	glUniform4f(shapes_->ts_loc,lx,-1+2/16.f,0.45/ar,1/12.f);
	
	draw_line(0.f,1.f,1.f,0);
	draw_line(0.f,-1.f,1.f,0);
	draw_line(-1.f,0.f,1.f,1);
	draw_line(1.f,0.f,1.f,1);
}

int read_data(double time, float* audio) {
	if(time < 0)
		return 1;
	
	int64_t t = (int64_t)round(time)*(SAMPLE_RATE/1000.0)*DATA_STRIDE*sizeof(float);
	int count = 0;
	
	LARGE_INTEGER filepos, li;
	li.QuadPart = 0;
	if(!SetFilePointerEx(hFile,li,&filepos,FILE_CURRENT)) {
		printf("CAN'T GET FILE POSITION: %ld",GetLastError());
		return 0;
	}

	DWORD bytesRead;
	while(t-DATA_STRIDE*sizeof(float) >= filepos.QuadPart) {
		while(ReadFile(hFile,filebuffer,sizeof(float)*DATA_STRIDE,&bytesRead,NULL) && DCOUNT < DATASIZE) {
			if(bytesRead != DATA_STRIDE*sizeof(float))
				return 0;
			for(int i = 0; i < NCHANNELS*2; i++)
				spikeline[i].data[DCOUNT] = filebuffer[i];
			filepos.QuadPart += bytesRead;
			DCOUNT++;
			count++;
		}

		DCOUNT = 0;
		int s = -1;
		for(int i = 0; i < NCHANNELS*2; i++) {
			if(!spikeline[i].enabled)
				continue;
			if(i == TARGET_CHANNEL) {
				if(AUDIOTYPE == 0)
					s = bandpass(spikeline[i].data,audiobuffer,80,DATASIZE,&spikeline[i].bp);
				else
				{
					s = bandpass(spikeline[i].data,NULL,80,DATASIZE,&spikeline[i].bp);
					for(int j = 0; j < DATASIZE; j++) {
						audiobuffer[j] = pow(spikeline[i].data[j]/50.f,2);
					}
				}
				if(liveplay) {
					if(BASS_StreamPutData(audio_->stream,audiobuffer,DATASIZE*sizeof(float)) == -1)
						fprintf(stderr,"CAN'T PUT AUDIO %d\r",BASS_ErrorGetCode());
				}
				else if(count <= (int)(1/FPS*SAMPLE_RATE)) {
					memcpy(audio+(count-DATASIZE),audiobuffer,DATASIZE*sizeof(float)); 
				}
			}
			else
				s = bandpass(spikeline[i].data,NULL,80,DATASIZE,&spikeline[i].bp);

			memcpy(spikeline[i].tracedata[TRACEBUF]+TRACESIZE+DTRACE,spikeline[i].data,sizeof(float)*DATASIZE);
			memcpy(spikeline[i].tracedata[!TRACEBUF]+DTRACE,spikeline[i].data,sizeof(float)*DATASIZE);

			if(s != 0)
			{
				if(s < 20)
				{
					if(DTRACE == 0)
						continue;
					else
						memcpy(spikeline[i].spikes[spikeline[i].currSpike%MAX_SPIKES].data,spikeline[i].tracedata[TRACEBUF]+(DTRACE-(20-s)),sizeof(float)*SPIKE_WIDTH);
				}
				else
					memcpy(spikeline[i].spikes[spikeline[i].currSpike%MAX_SPIKES].data,spikeline[i].data+(s-20),sizeof(float)*SPIKE_WIDTH);
				
				spikeline[i].spikes[spikeline[i].currSpike%MAX_SPIKES].timestamp = time-(t-filepos.QuadPart)/(SAMPLE_RATE/1000.f)/DATA_STRIDE/sizeof(float);
				if(spikeline[i].spikes[spikeline[i].currSpike%MAX_SPIKES].active == 0)
				{
					spikeline[i].spikes[spikeline[i].currSpike%MAX_SPIKES].active = 1;
					spikeline[i].spikecount++;
				}
				spikeline[i].currSpike++;
			}
		}
		
		DTRACE += DATASIZE;
		if(DTRACE >= TRACESIZE)
		{
			DTRACE = 0;
			TRACEBUF = !TRACEBUF;
		}
	}
	
	return 1;
}

int read_raster(double time) {
	if(time < 0)
		return 1;
	
	int64_t t = (int64_t)round(time)*(SAMPLE_RATE/1000.0)*DATA_STRIDE*sizeof(float);
	int count = 0;
	
	LARGE_INTEGER filepos, li;
	li.QuadPart = 0;
	if(!SetFilePointerEx(rFile,li,&filepos,FILE_CURRENT)) {
		printf("CAN'T GET FILE POSITION: %ld",GetLastError());
		return 0;
	}

	DWORD bytesRead;
	while(t-DATA_STRIDE*sizeof(float) >= filepos.QuadPart) {
		while(ReadFile(rFile,filebuffer,sizeof(float)*DATA_STRIDE,&bytesRead,NULL) && count < DATASIZE) {
			if(bytesRead != DATA_STRIDE*sizeof(float))
				return 0;
			for(int i = 0; i < NCHANNELS*2; i++)
				rasterline[i].data[count] = filebuffer[i];
			filepos.QuadPart += bytesRead;
			count++;
		}
		
		int s = -1;
		count = 0;
		for(int i = 0; i < NCHANNELS*2; i++) {
			s = bandpass(rasterline[i].data,NULL,80,DATASIZE,&rasterline[i].bp);
			if(s != 0)
				add_raster(i,time-(t-filepos.QuadPart)/(SAMPLE_RATE/1000.f)/DATA_STRIDE/sizeof(float));
		}
	}
	
	return 1;
}

void cull_spikes(double time) {
	for(int i = 0; i < NCHANNELS*2; i++) {
		for(int j = 0; j < MAX_SPIKES; j++) {
			if(spikeline[i].spikes[j].active) {
				if((time - spikeline[i].spikes[j].timestamp)/1000 >= 1.f) {
					spikeline[i].spikes[j].active = 0;
					spikeline[i].spikecount--;
				}
			}
		}
	}
}

void destroy_neural() {
	CloseHandle(rFile);
	CloseHandle(hFile);
	
	glDeleteVertexArrays(1, &lineVAO);
	glDeleteBuffers(1, &lineVBO);
	glDeleteProgram(shader);
	
	for(int i = 0; i < NCHANNELS*2; i++) {
		for(int j = 0; j < 2; j++) {
			free(spikeline[i].tracedata[j]);
		}
	}
	
	free(rasterline);
	free(spikeline);
}