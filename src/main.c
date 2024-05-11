#include <windows.h>
#include <glad/glad.h>

#include <video.h>
#include <audio.h>
#include <bandpass.h>
#include <mux.h>
#include <neural.h>
#include <sessionreader.h>
#include <text.h>
#include <shapes.h>
#include <image.h>
#include <sqlite3.h>

#include <dirent.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

struct bhv_struct {
	int trialnum;
	int gamenum;
	float timestamp;
};

int load_alignment();
int load_bhv();
int load_settings();
	
static char DATA_FILE[MAX_PATH],SESSION_FILE[MAX_PATH],ALIGNMENT_FILE[MAX_PATH],BEHAVIOR_FILE[MAX_PATH],SETTINGS_FILE[MAX_PATH];
static int NCHANNELS = 64, GAME_START = 0, PAUSE = 0, click = 0;
double coeffA,coeffB;

unsigned long trialcount = 0, gamecount = 0;
struct bhv_struct* bhv_data = NULL;

HSTREAM stream;

static int process_messages()
{
	MSG msg;
	int result_ = 0;

	while(PeekMessage(&msg, video_->hWnd, 0, 0, PM_NOREMOVE))
	{
		if(GetMessage(&msg, NULL, 0, 0))
		{
			TranslateMessage(&msg); 
			
			switch(msg.message) {
				case WM_KEYDOWN:
					switch (msg.wParam) {
					case VK_ESCAPE:		
						result_ = -1;
					break;
					case VK_F8:
						BASS_ChannelPlay(stream,TRUE);
					break;
					case VK_SPACE:
						PAUSE = !PAUSE;
					break;
					}
				break;
				case WM_LBUTTONDOWN:
					click = 1;
				break;
				/*case WM_MOUSEWHEEL:
					int delta = GET_WHEEL_DELTA_WPARAM(msg.wParam);

					SCROLLPOS -= (delta<0?-1:1);
					if(SCROLLPOS < 0)
						SCROLLPOS = 0;
				break;*/
			}
			DispatchMessage(&msg); 
		}
		else
			result_ = -1;
	}
						
	return result_;
}

static int get_files(char* directory)
{
	DIR* d;
	struct dirent* dir;
	int n[] = {0,0,0,0,0};
	
	memset(DATA_FILE,0,sizeof(DATA_FILE));
	memset(SESSION_FILE,0,sizeof(SESSION_FILE));
	memset(ALIGNMENT_FILE,0,sizeof(ALIGNMENT_FILE));
	memset(BEHAVIOR_FILE,0,sizeof(BEHAVIOR_FILE));
	memset(SETTINGS_FILE,0,sizeof(SETTINGS_FILE));

	d = opendir(directory);
	if(d != NULL) {
		while((dir = readdir(d)) != NULL) {
			if(strstr(dir->d_name,".xdat")) {
				sprintf(DATA_FILE,"%s%s",directory,dir->d_name);
				n[0] = 1;
			}
			else if(strstr(dir->d_name,".sqlite")) {
				sprintf(SESSION_FILE,"%s%s",directory,dir->d_name);
				n[1] = 1;
			}
			else if(strstr(dir->d_name,".txt")) {
				sprintf(ALIGNMENT_FILE,"%s%s",directory,dir->d_name);
				n[2] = 1;
			}
			else if(strstr(dir->d_name,".bhv")) {
				sprintf(BEHAVIOR_FILE,"%s%s",directory,dir->d_name);
				n[3] = 1;
			}
			else if(strstr(dir->d_name,".ini")) {
				sprintf(SETTINGS_FILE,"%s%s",directory,dir->d_name);
				n[4] = 1;
			}
		}
	} else {
		printf("CANT OPEN DIRECTORY\n");
		return -1;
	}
	
	int quit = 0;
	for(int i = 0; i < 5; i++) {
		if(!n[i]) {
			quit = 1;
			printf("[ERROR]");
			switch(i) {
				case 0:
					printf("Missing NEURAL (.xdat) File\n");
				break;
				case 1:
					printf("Missing SESSION (.sqlite) File\n");
				break;
				case 2:
					printf("Missing ALIGNMENT (.txt) File\n");
				break;
				case 3:
					printf("Missing BEHAVIOR (.bhv) File\n");
				break;
				case 4:
					printf("Missing SETTINGS (.ini) File\n");
				break;
			}
		}
	}
	
	if(quit)
		return -1;
	
	return 0;
}

static int scan_directory()
{
	DIR* d;
	struct dirent* dir;
	char **dirlist = NULL;
	int dircount = 0;

	d = opendir("Sessions");
	if(d != NULL) {
		while((dir = readdir(d)) != NULL) {
			if(!strstr(dir->d_name,".")) {
				dirlist = (char**)realloc(dirlist,sizeof(char*)*(dircount+1));
				dirlist[dircount] = (char*)malloc(strlen(dir->d_name)+1);
				memcpy(dirlist[dircount],dir->d_name,strlen(dir->d_name)+1);
				dircount++;
			}
		}
	} else {
		fprintf(stderr,"CANT OPEN DIRECTORY\n");
		return 0;
	}
	
	if(dircount == 0) {
		fprintf(stderr,"No Directories Found\n");
		return 0;
	}

	printf("(%d) Directories Found:\n",dircount);
	for(int i = 0; i < dircount; i++)
		printf("%d. %s\n",i+1,dirlist[i]);
	
	int n = 1;
	if(dircount > 1) {
		printf("\nSelect a directory: ");
		scanf("%d",&n);
		while(n <= 0 || n > dircount) {
			printf("Invalid Selection\nSelect a directory: ");
			fflush(stdin);
			scanf("%d",&n);
		}
	}
	
	printf("\n");
	
	char buffer[MAX_PATH];
	sprintf(buffer,"Sessions/%s/",dirlist[n-1]);

	for(int i = 0; i < dircount; i++)
		free(dirlist[i]);
	free(dirlist);
	
	fflush(stdin);
	
	return get_files(buffer);
}

int main(int argc, char *argv[])
{	
	float* recorded_audio = NULL;
	float firstframe = 0.f;
	int SAMPLE_RATE = 30000;
	
	if(scan_directory() == -1) {
		printf("\nPress Enter to Quit");
		getchar();
		return 1;
	}
	
	printf("Options:\n1. Live Viewer\n2. Record Video\n\nSelect an option: ");
	scanf("%d",&liveplay);
	while(liveplay != 1 && liveplay != 2) {
		printf("Invalid Selection\nSelect an option: ");
		fflush(stdin);
		scanf("%d",&liveplay);
	}
	
	liveplay %= 2;
	
	if(!load_alignment() || !load_bhv() || !load_settings())
		return 1;
	
	//INIT AUDIO
	printf("\nINITIALIING AUDIO...\n");
	if(liveplay) {
		if(!init_audio())
			return 1;
	} else {
		recorded_audio = (float*)malloc(sizeof(float)*((int)(1/FPS*SAMPLE_RATE)));
		memset(recorded_audio,0,sizeof(float)*((int)(1/FPS*SAMPLE_RATE)));
	}
	
	//INIT VIDEO
	printf("INITIALIING VIDEO...\n");
	if(liveplay) {
		if(!init_video(1440,810,0,(WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_SIZEBOX),PFD_DOUBLEBUFFER))
			return 1;
	} else {
		if(!init_video(1920,1080,0,WS_POPUP,PFD_DOUBLEBUFFER))
			return 1;
	}
	
	//INIT GRAPHICS
	if(!init_text() || !init_shapes() || !init_image())
		return 1;

	//INIT SESSION PLAYER
	printf("LOADING BEHAVIORAL DATA...\n");
	if(!init_session(SESSION_FILE,&firstframe)) {
		return 1;
	}
	
	double elapsed = 0.0, aligntime = 0.0, rastertime = 0.0, offset = 0.0, record_length = 0, lastdraw = 0.0;
	int firsttrial = -1, currtrial = 0, lasttrial = trialcount-1, currgame = 0;
	int GAME_END = 0;
	
	LARGE_INTEGER freq,start,end;
	QueryPerformanceFrequency(&freq); 
	QueryPerformanceCounter(&start);
	
	//SELECT TRIAL START
	if(!liveplay) {
		fflush(stdin);
		printf("\n\nEnter game range in format \"x y\" (%ld Total games): ",gamecount);
		scanf("%d %d",&GAME_START,&GAME_END);
		while(GAME_START <= 0 || GAME_START > gamecount || GAME_END <= 0 || GAME_END > gamecount || GAME_END < GAME_START) {
			printf("Invalid range\n");
			printf("Enter game range in format \"x y\" (%ld Total games): ",gamecount);
			fflush(stdin);
			scanf("%d %d",&GAME_START,&GAME_END);
		}
		
		fflush(stdin);
		printf("\nEnter target channel #: ");
		scanf("%d",&TARGET_CHANNEL);
		if(TARGET_CHANNEL < 0)
			TARGET_CHANNEL = 0;
		else if(TARGET_CHANNEL >= NCHANNELS*2)
			TARGET_CHANNEL = NCHANNELS*2-1;
	} else {
		fflush(stdin);
		printf("\nEnter start game #: ");
		scanf("%d",&GAME_START);
	}
	
	for(int i = 0; i < trialcount; i++) {
		if(bhv_data[i].gamenum >= GAME_START && firsttrial == -1) {
			firsttrial = i;
			if(liveplay)
				break;
		}
		else if(!liveplay && bhv_data[i].gamenum > GAME_END) {
			lasttrial = i;
			break;
		}
	}
	offset = (bhv_data[firsttrial].timestamp)/CLOCKS_PER_SEC;
	currtrial = firsttrial;
	
	//INIT NEURAL
	printf("\nInitializing Neural Data...\n");
	currgame = GAME_START;
	if(!init_neural(DATA_FILE,64,134,150000,SAMPLE_RATE,(coeffA+(offset-(liveplay?0.f:5.f))*coeffB+fabs(coeffA*2))*CLOCKS_PER_SEC))
		return 1;
	
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glClearColor(0.f,0.f,0.f,1.f);
	
	struct audio_data* rewclick;
	int rewpos = 0;
	if(liveplay) {
		printf("\nTRIAL START: %d\n",GAME_START);
		ShowWindow(video_->hWnd,SW_SHOW);
	} else {
		if(init_record("out.mp4",video_->w,video_->h,&recorded_audio) == -1) {
			fflush(stdin);
			getchar();
			return -1;
		}
		rewclick = load_mp3("reward.mp3");
		if(rewclick == NULL) {
			printf("Failed to load reward sound\n");
		}
		
		printf("\nRendering Output...\n");
		record_length = bhv_data[lasttrial].timestamp-bhv_data[firsttrial].timestamp;
	}
	
	elapsed = offset*CLOCKS_PER_SEC;
	lastdraw = elapsed;
	
	int return_code = 0;
	int reward = 0;
    // render loop
    // -----------
    while(!return_code)
    {
		QueryPerformanceCounter(&end);
		
		if(PAUSE == 0)
		{
			if(liveplay)
				elapsed = (end.QuadPart - start.QuadPart)*1000/freq.QuadPart+offset*CLOCKS_PER_SEC;
			else
				elapsed += 1/FPS*CLOCKS_PER_SEC;
			aligntime = (coeffA+elapsed/CLOCKS_PER_SEC*coeffB+fabs(coeffA*2))*CLOCKS_PER_SEC;
			rastertime = aligntime+1250;
		}
		else if(PAUSE == 1)
		{
			offset = elapsed/CLOCKS_PER_SEC;
			start = end;
		}
		
		if(!read_data(aligntime,recorded_audio))
			break;
		if(!read_raster(rastertime))
			break;
		
		cull_spikes(aligntime);
		
		if((!liveplay) || (elapsed-lastdraw >= 1/30.f*CLOCKS_PER_SEC) || PAUSE)
		{	
			float ar = ((float)video_->w/video_->h)/(4.f/3), lx = -0.6, ly = 0.21;
			glClear(GL_COLOR_BUFFER_BIT);
			
			draw_session(elapsed,ar,lx,0.5+ly,0.5,0.5);	

			while(currtrial < trialcount && elapsed > bhv_data[currtrial].timestamp) {
				currgame = bhv_data[currtrial].gamenum;
				currtrial++;
			}
			
			//Draw info text
			if(liveplay)
			{
				char buf[64];
				sprintf(buf,"TRIAL: %d\nGAME: %d",currtrial,abs(currgame));
				
				glUniform4f(text_->ts_loc,0.f,0.f,1.f,1.f);
				glUniform4f(text_->colu_loc,1.f,1.f,1.f,1.f);
				render_simpletext(buf,-1.f,1.f,2,TXT_TOPALIGNED);
			}
			
			draw_spikes(aligntime);
			draw_raster(rastertime, lx, ly); 
			
			if(click) {
				POINT mousepos;
				double mx, my;
				float x1 = -0.2, x2=x1+1.2;
				int row = -1, column = -1;
				
				GetCursorPos(&mousepos);
				ScreenToClient(video_->hWnd,&mousepos);
				
				mx = (float)mousepos.x/(video_->w)*2.f-1.f-1/20.f;
				my = (float)mousepos.y/(video_->h);
				
				row = my*13;
				if(mx > (x2-0.6) && mx - (x2-0.6) < 0.5) {
					column = (int)((mx-(x2-0.6))*10);
					TARGET_CHANNEL = column*13+row+NCHANNELS;
					if(TARGET_CHANNEL >= NCHANNELS*2)
						TARGET_CHANNEL = NCHANNELS*2-1;
					BASS_ChannelPlay(audio_->stream,TRUE);
				}
				else if(mx > x1 && mx-x1 < 0.5) {
					column = (int)((mx-x1)*10);
					TARGET_CHANNEL = column*13+row;
					if(TARGET_CHANNEL >= NCHANNELS)
						TARGET_CHANNEL = NCHANNELS-1;
					BASS_ChannelPlay(audio_->stream,TRUE);
				}
				click = 0;
			}
			
			if(!liveplay) {
				if(currtrial > lasttrial)
					break;
			}
			
			SwapBuffers(video_->hDC);
			lastdraw = elapsed;
			
			if(!liveplay) {
				if(reward) {
					if(rewpos < rewclick->samples) {
						for(int i = 0; i < ((int)(1/FPS*SAMPLE_RATE)); i++) {
							if(rewpos+i >= rewclick->samples)
								break;
							recorded_audio[i] = recorded_audio[i]*0.5 + rewclick->data[rewpos+i]*0.5;
						}
						rewpos += ((int)(1/FPS*SAMPLE_RATE));
					}
					else {
						rewpos = 0;
						reward = 0;
					}
				}
				add_frame();
				printf("%.2f%%\r",(float)(elapsed-bhv_data[firsttrial].timestamp)/record_length*100.f);
			}
		}
		return_code = process_messages();
		
		if(PAUSE)
			Sleep(30);
    }
	
	if(!liveplay) {
		printf("\nFinished processing.\n");
		stop_record();
		free(recorded_audio);
		fflush(stdin);
		printf("\nPress enter to exit.");
		getchar();
	} else {
		destroy_audio();
	}
	
	destroy_neural();
	destroy_text();
	destroy_shapes();
    destroy_video();	
	
    return 0;
}

int load_alignment() {
	FILE* fp = fopen(ALIGNMENT_FILE,"r");
	if(!fp) {
		fprintf(stderr,"Can't Open Alignement File\n");
		return 0;
	}
	
	char buffer[256];
	
	//Coefficient A
	if(fgets(buffer,sizeof(buffer),fp)) {
		char* tok = strtok(buffer," ");
		tok = strtok(NULL," ");
		coeffA = atof(tok);
	}
	
	//Coefficient B
	if(fgets(buffer,sizeof(buffer),fp)) {
		char* tok = strtok(buffer," ");
		tok = strtok(NULL," ");
		coeffB = atof(tok);
	}
	
	fclose(fp);
	
	if(coeffA == 0 || coeffB == 0) {
		fprintf(stderr,"Unable to Read Alignment Coefficients\n");
		return 0;
	}
	
	return 1;
}

int load_bhv() {
	FILE* fp = fopen(BEHAVIOR_FILE,"r");
	if(!fp) {
		fprintf(stderr,"Unable to Open Behavior File\n");
		return 0;
	}
	
	// count first to preallocate space
	char buffer[256];
	int count = 0;
	while(fgets(buffer,sizeof(buffer),fp)) {
		count++;
	}
	
	if(!count) {
		fprintf(stderr,"Behavior File Empty\n");
		return 0;
	}
	
	bhv_data = (struct bhv_struct*)malloc(sizeof(struct bhv_struct)*count);
	memset(bhv_data,0,sizeof(struct bhv_struct)*count);
	rewind(fp);
	count = 0;
	
	while(fgets(buffer,sizeof(buffer),fp)) {
		sscanf(buffer,"%d %d %f",&bhv_data[count].trialnum,&bhv_data[count].gamenum,&bhv_data[count].timestamp);
		count++;
	}
	
	fclose(fp);
	
	trialcount = count;
	gamecount = bhv_data[count-1].gamenum;
	
	return 1;
}

int load_settings() {
	FILE* fp = fopen(SETTINGS_FILE,"r");
	if(!fp) {
		fprintf(stderr,"Unable to Open Settings File\n");
		return 0;
	}
	
	char buffer[256];
	while(fgets(buffer,sizeof(buffer),fp)) {
		if(strstr(buffer,"GAME_START=")) {
			sscanf(buffer,"GAME_START=%d",&GAME_START);
		}
	}
	
	fclose(fp);
	
	TARGET_CHANNEL = 14;
	
	return 1;
}